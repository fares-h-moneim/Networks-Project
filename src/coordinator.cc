//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "coordinator.h"
#include <fstream>
Define_Module(Coordinator);

void Coordinator::initialize()
{
    // TODO - Generated method body
    std::ifstream inputFile("../src/coordinator.txt");

       if (inputFile.is_open()) {
           // Read the first number from the file
           int firstNumber;
           inputFile >> firstNumber;

           double delay;
           inputFile>>delay;

           // Close the file
           inputFile.close();

           // Send message to the appropriate node based on the first number
           if (firstNumber == 0) {
               cMessage *msg = new cMessage("Hello from Coordinator to Node0");
               sendDelayed(msg, delay, "out_gate0");
           } else if (firstNumber == 1) {
               cMessage *msg = new cMessage("Hello from Coordinator to Node1");
               sendDelayed(msg, delay, "out_gate1");
           } else {
               EV << "Invalid first number read from the file" << endl;
           }
       } else {
           EV << "Error opening file coordinator.txt" << endl;
       }
}

void Coordinator::handleMessage(cMessage *msg)
{
    // TODO - Generated method body
}
