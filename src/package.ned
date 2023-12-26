package networksproject;

@license(LGPL);
//
// TODO documentation
//
network Network
{
    @display("bgb=359,163");
    submodules:
        coordinator: Coordinator {
            @display("p=152,119");
        }
        node0: Node {
            @display("p=228,32");
        }
        node1: Node {
            @display("p=74,32");
        }
    connections:
        coordinator.out_gate0 --> node0.coordinator_gate;
        coordinator.out_gate1 --> node1.coordinator_gate;
        node1.out_gate --> node0.in_gate;
        node0.out_gate --> node1.in_gate;
}

