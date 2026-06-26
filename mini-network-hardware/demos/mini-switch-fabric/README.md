# mini-switch-fabric — Switch Fabric Architecture

## Overview

A switch fabric is the internal interconnection network that moves data between the ports of a network switch. The design of the switch fabric fundamentally determines the switch's throughput, latency, and scalability. This document explores switch fabric architectures as modeled in `switch_fabric.h` and `switch_fabric.c`, covering crossbar, shared memory, and bus-based designs, along with queuing strategies and the Head-of-Line (HOL) blocking problem.

## Switch Architecture Overview

A network switch has three main components:

```
                 +-----------------------------------+
                 |           CONTROL PLANE            |
                 |  (Routing protocols, MAC learning, |
                 |   STP, management)                 |
                 +-----------------+------------------+
                                   |
                 +-----------------+------------------+
                 |           SWITCH FABRIC              |
                 |  (Crossbar / Shared Memory / Bus)    |
                 +--+-----+-----+-----+-----+-----+---+
                    |     |     |     |     |     |
                 +--+-+ +-+-+ +-+-+ +-+-+ +-+-+ +-+--+
                 | P0 | | P1 | | P2 | | P3 | |...| | Pn|
                 +----+ +----+ +----+ +----+ +----+ +----+
```

- **Ports**: Connect to external links; handle MAC/PHY, packet buffering, classification
- **Switch Fabric**: Interconnects ports, providing a data path between any pair
- **Control Plane**: Makes forwarding decisions and programs the fabric

## Crossbar Switch Fabric

### Architecture

A crossbar is a matrix of NxN crosspoints that can connect any input to any output without blocking:

```
        Output 0   Output 1   Output 2   Output 3
         |          |          |          |
Input 0 -+----------+----------+----------+----
         |   X      |          |          |
Input 1 -+----------+----------+----------+----
         |          |   X      |          |
Input 2 -+----------+----------+----------+----
         |          |          |   X      |
Input 3 -+----------+----------+----------+----
         |          |          |          |   X
```

Each 'X' represents a crosspoint switch that can be turned on/off. The `crossbar_matrix` in our implementation maps these connections: `crossbar_matrix[i][j]` is true when input i is connected to output j.

### Properties
| Property       | Value        |
|----------------|--------------|
| Complexity     | O(N²)        |
| Blocking       | Non-blocking (can connect any idle input to any idle output) |
| Throughput     | Up to N simultaneous connections at line rate |
| Scalability    | Limited by O(N²) physical crosspoints |

### Scheduling the Crossbar

For each time slot, a matching must be computed that pairs inputs to outputs without conflicts. This is a bipartite graph matching problem:

- **Maximum Size Matching**: Maximizes number of connections
- **Maximum Weight Matching**: Each input-output pair has a weight (e.g., queue length); maximizes total weight
- **iSLIP**: Practical iterative algorithm; O(log N) iterations to converge

## Shared Memory Switch

### Architecture

In a shared-memory switch, input data is written to a common memory pool and outputs read from it:

```
+--------+     +--------+
| Port 0 | <-> |        |
+--------+     |        |
+--------+     | Shared |     +--------+
| Port 1 | <-> | Memory | <-> | Output |
+--------+     | Buffer |     |   MUX  |
     ...       |        |     +--------+
+--------+     |        |
| Port N | <-> |        |
+--------+     +--------+
```

### Properties
| Property       | Value        |
|----------------|--------------|
| Complexity     | O(N) memory bandwidth |
| Memory Usage   | Efficient statistical sharing |
| Scalability    | Limited by memory bandwidth (must support N writes + N reads simultaneously) |
| Multicast       | Natural: one write, multiple reads |

### Implementation Challenges

- **Memory bandwidth**: Each time slot requires N reads + N writes at line rate
- **Memory access time**: SRAM for performance, DRAM for capacity
- **Address management**: Free list of buffer addresses, linked lists per output queue

## Bus-Based Switch

### Architecture

All ports share a common bus; only one transmission at a time:

```
+--------+  +--------+  +--------+  +--------+
| Port 0 |  | Port 1 |  | Port 2 |  | Port 3 |
+---+----+  +---+----+  +---+----+  +---+----+
    |          |          |          |
    +----------+----------+----------+---- Backplane Bus
```

### Properties
| Property       | Value        |
|----------------|--------------|
| Complexity     | O(1)         |
| Throughput     | Bus bandwidth limits total throughput |
| Scalability    | Poor; all ports share single bus |
| Cost           | Low          |

Bus-based switches are common in small/low-cost switches. The shared bus becomes the bottleneck as port count increases.

## Output Queuing and Head-of-Line Blocking

### Output Queuing

In an output-queued switch, packets are immediately forwarded through the fabric to the output port's buffer:

```
Inputs ---> [Fabric: Nx speedup] ---> Output Queue
                                           |
                                           v
                                        Output Link
```

The fabric must run at N times the line rate to avoid packet loss when multiple inputs send to the same output simultaneously. This is impractical for large N.

### Head-of-Line (HOL) Blocking

In an input-queued switch, each input has a single FIFO queue. If the packet at the head of the queue is destined for a busy output, it blocks all following packets — even those destined for idle outputs:

```
Input Queue 0: [A->2] [B->1] [C->3] ...
                  ^ HOL blocks B and C even though outputs 1 and 3 are idle
```

This reduces throughput to approximately 58.6% of line rate under uniform traffic (Karol et al., 1987).

### Virtual Output Queues (VOQs)

The solution: maintain a separate queue at each input for each output:

```
Input 0:
  VOQ to Output 0: [  ] [  ]
  VOQ to Output 1: [A->1] [B->1]
  VOQ to Output 2: [C->2]
  VOQ to Output 3: [  ]
```

The scheduler can select any non-empty VOQ at each input, matching it to an available output. This eliminates HOL blocking and achieves 100% throughput with an appropriate scheduling algorithm.

## Switching Fabrics in Practice

| Fabric Type    | Example Switches           | Use Case                         |
|----------------|----------------------------|----------------------------------|
| Crossbar       | Cisco Nexus 9000, Arista   | Data center spine/leaf           |
| Shared Memory  | Cisco Catalyst 6500        | Enterprise core                  |
| Bus            | Budget 8-port switches     | SOHO                             |
| Clos (multi-stage) | Juniper QFX10000       | Hyperscale data centers          |

## Scheduling Policies

### FIFO (First-In-First-Out)
- Simplest; packets served in arrival order
- No QoS differentiation; susceptible to HOL blocking

### Priority Queuing (SPQ)
- High-priority traffic always served first
- Risk of starvation for low-priority traffic

### Weighted Round Robin (WRR)
- Each queue has a weight; service is proportional to weight
- Provides fairness with prioritization
- Our implementation uses `SCHED_WRR` for weighted scheduling

## Implementation in `switch_fabric.c`

The `SwitchFabric` structure captures essential switch elements:

- **Ports array**: Each `SwitchPort` contains MAC table, VLAN, speed, and statistics
- **Crossbar matrix**: Boolean matrix representing input-output connections
- **Shared buffer**: Model for shared-memory switching (64 KB)
- **Scheduling policy**: Selectable FIFO, Priority, or WRR

Key functions:

| Function               | Operation                                           |
|------------------------|-----------------------------------------------------|
| `switch_init()`        | Allocates and zeroes a switch with N ports          |
| `switch_add_port()`    | Configures a port with specified speed              |
| `switch_learn_mac()`   | Learns source MAC address → port mapping            |
| `switch_forward()`     | Looks up destination MAC and forwards               |
| `switch_flood()`       | Floods frame to all ports except source             |
| `switch_crossbar_route()` | Sets up a crossbar connection between ports      |

## References

- Stanford CS144: Switching and Forwarding, Bridging
- Nick McKeown, "The iSLIP Scheduling Algorithm for Input-Queued Switches"
- Karol, Hluchyj, Morgan, "Input Versus Output Queueing on a Space-Division Packet Switch" (IEEE Trans. Comm., 1987)
- M. Arsalan et al., "Survey and Taxonomy of Switch Fabrics"
- Cisco Nexus 9000 CloudScale Architecture

---

*Generated for mini-network-hardware project*
