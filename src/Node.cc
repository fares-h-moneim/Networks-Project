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

#include "Node.h"
#include "Message_m.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <bitset>

std::vector<std::string> regularMessages;
std::vector<std::string> errorMessages;
int currentMessageIndex = 0;
bool node0sender = false;
bool node1sender = false;
std::string sender = "";
std::string reciever = "";
Define_Module(Node);



//SELECTIVE REPEAT PROTOCOL STARTS HERE
int senderWindowStart = 0;
int senderWindowEnd = 0;
double senderWindowSize = 0;
int receiverWindowStart = 0;
double receiverWindowSize = 0;
std::vector<Message_Base*> senderWindow;
std::vector<Message_Base*> receiverWindow;
std::vector<bool> ackedMessages(false);

double senderTimeout;
double processingDelay;
double duplicationDelay;
double transmissionDelay;
double errorDelay;
int expectedSeqNum = 0;

std::ofstream outputFile;

char summing(std::string s)
{
    int ans = 0;
    for (int i = 0; i < s.size(); i++)
    {
        ans += s[i];
    }
    return (char)ans;

}
char checksum(std::string s) {

    return ~summing(s);
}

std::string frameEncoding(const std::string &payload)
{
    std::string temp = "#";
    for (auto &c : payload)
    {
        if (c == '#'){
            temp += "/#";
        }
        else if (c == '/'){
            temp += "//";
        }
        else{
            temp += c;
        }
    }
    temp += '#';
    return temp;
}

std::string frameDecoding(const std::string &payload)
{
    std::string temp = "";
    for (int i = 1; i < payload.length() - 1; i++){
        if (payload[i] == '/' && payload[i + 1] == '/'){
            temp += '/';
            i++;
        }
        else if (payload[i] == '/' && payload[i + 1] == '#'){
            temp += '#';
            i++;
        }
        else if (payload[i] == '/'){
            continue;
        }
        else if (payload[i] == '#'){
            break;
        }
        else{
            temp += payload[i];
        }
    }
    return temp;
}

void Node::initialize()
{
    // TODO - Generated method body
    senderWindowSize = par("SenderWindow").doubleValue();
    receiverWindowSize = par("RecieverWindow").doubleValue();
    senderWindow.resize(senderWindowSize);
    std::fill(senderWindow.begin(), senderWindow.end(), nullptr);
    ackedMessages.resize(senderWindowSize, false);
    //ackedMessages(1,false); //3ayz a initalize b false
    receiverWindow.resize(receiverWindowSize);
    std::fill(receiverWindow.begin(), receiverWindow.end(), nullptr);

    senderTimeout = par("SenderTimeOut").doubleValue();
    processingDelay = par("ProcessingTime").doubleValue();
    duplicationDelay = par("DuplicationDelay").doubleValue();
    transmissionDelay = par("TransmissionDelay").doubleValue();
    errorDelay = par("ErrorDelay").doubleValue();
}


/*sendMessages() function sends the frames in the current window and is called when an ack
 *  is recieved and window is moved to send new messages*/
void Node::sendMessages()
{
    //the variable i is corelated  for the output of processing delay for the outputfile
    int i=1;
    //while loop starts with 2 pointers at the start of the window
    //after sending every message we increase the senderWindowEnd
    //send until we send all the messages in the window OR there are no messages to send anymore
    while(senderWindowEnd - senderWindowStart < senderWindow.size() && currentMessageIndex < regularMessages.size())
    {
        //sending the message now
        Message_Base * mmsg = new Message_Base(regularMessages[currentMessageIndex].c_str());
        //frametype 0 data 1 ack 2 nack
        mmsg->setFrameType(0);
        //setting the checksum
        char checksum = summing(regularMessages[currentMessageIndex]);
        mmsg->setChecksum(checksum);
        //setting payload with the message (THIS IS FRAMED WHEN WE ACTUALLY SEND!)
        mmsg->setPayload(regularMessages[currentMessageIndex].c_str());
        //setting sequence number (we are working linearly not circular TA Hisa said thats okay)
        mmsg->setSeq_Num(currentMessageIndex);
        //putting message in my buffer to track what i have sent (needed in resends)
        senderWindow[currentMessageIndex%senderWindow.size()] = mmsg;
        //output file of introducing the channel error
        outputFile << "At time [" << simTime() + (i-1)*(processingDelay) << "], Node[Sender], Introducing channel error with code =[" << errorMessages[currentMessageIndex] << "]. and msg = " << regularMessages[currentMessageIndex]<< std::endl;
        std::string errorCode = errorMessages[currentMessageIndex];
        //HERE WE SEND ACTUAL MESSAGE!
        applyErrorBehavior(mmsg, errorCode, i);
        //increase to send next message
        currentMessageIndex++;
        //This is for the timeout of the message with the sendertimeoutdelay. This is handled when we recieve the self message!
        scheduleAt(simTime()+senderTimeout+transmissionDelay+processingDelay, new cMessage(std::to_string(mmsg->getSeq_Num()).c_str()));
        senderWindowEnd++;
        i++;
    }
}

//Function to see if seqNum is within reciever window :) Need it for recieveMessage
bool isWithinRecieverWindow(int seqNum)
{
    return (seqNum >= receiverWindowStart && seqNum < receiverWindowStart + receiverWindowSize);
}

//Function to recieve message when it arrives!
void Node::receiveMessage(Message_Base *msg)
{
    //this bool just for making the output statement (if im acked or nacked)
    bool isnacked = false;
    //getting seqNum
    int seqNum = msg->getSeq_Num();
    //bool needed to handle if i came out of order so i dont send an ack or nack for that frame!
    bool icansend = true;
    //i arrive in recieve so im outputing that i have recieved something
    outputFile << "At time[" << simTime() << "], Node[reciever] Received Frame with seq_num = " << msg->getSeq_Num() << " and payload = [" << msg->getPayload() << "] and trailer = "<< msg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[0], Delay[0]"<<endl;
    //will only send acks/nacks when im within the recieverwindow
    if(isWithinRecieverWindow(seqNum))
    {
        //if i got seqNum>expected i have to send nacks on frames that didnt come
        //the !ackedMessage part is to ensure if i already recieved this frame before (which was out of order) and not send nacks again (TA Hisa said not to send nack again if we already did before)
        if(seqNum>expectedSeqNum && !ackedMessages[seqNum%(int)receiverWindowSize])
        {
            //looping on all the frames that wasnt sent
            for(int i = expectedSeqNum; i<seqNum;i++)
            {
                //sending them nacks
                Message_Base*ack = new Message_Base("ack");
                ack->setSeq_Num(i);
                ack->setACK_NACK_Number(2);
                ack->setFrameType(2);
                outputFile << "At time[" << simTime() + (i+1-expectedSeqNum)*processingDelay << "], Node[reciever] Sending [NACK] with number [" << i << "] due to out of order frame recieved" << std::endl;
                sendDelayed(ack, (i+1-expectedSeqNum)*processingDelay + transmissionDelay, "out_gate");
            }
            //checking if the out of order frame is even correct or not
            char help = checksum(frameDecoding(msg->getPayload()));
            if((int)(help+msg->getChecksum()) == -1) //this means it is correct
            {
                receiverWindow[seqNum%(int)receiverWindowSize] = msg; //i will accept it and WILL NOT send ack on it until other frames come
                ackedMessages[seqNum%(int)receiverWindowSize] = true;
            }
            else //this means i need to send nack
            {
                Message_Base*ack = new Message_Base("ack");
                ack->setSeq_Num(seqNum);
                ack->setACK_NACK_Number(2);
                ack->setFrameType(2);
                outputFile << "At time[" << simTime() + processingDelay << "], Node[reciever] Sending [NACK] with number [" << seqNum << "] due to error in checksum. this frame is also out of order!" << std::endl;
                sendDelayed(ack, processingDelay + transmissionDelay, "out_gate");

            }
            //making icansend false to not send an ack
            icansend = false;
        }

        //here the message is the expectedframe or behind it (one of the ones that came before the expected)
        //the !ackedmessages just to make sure im not sending ack on something i recieved before
        if(icansend && !ackedMessages[seqNum%(int)receiverWindowSize])
        {
            Message_Base *ack = new Message_Base("ack");
            ack->setSeq_Num(seqNum);
            char help = checksum(frameDecoding(msg->getPayload()));
            if((int)(help+msg->getChecksum()) == -1) //this means its correct and will send ack
                {
                ack->setACK_NACK_Number(1);
                ack->setFrameType(1);
                receiverWindow[seqNum%(int)receiverWindowSize] = msg;
                expectedSeqNum++;
                }
            else //this means its wrong and will send nack
                {
                isnacked = true;
                ack->setACK_NACK_Number(2);

                ack->setFrameType(2);
                }
            sendDelayed(ack, processingDelay + transmissionDelay, "out_gate");
            outputFile << "At time[" << simTime() + processingDelay << "], Node[reciever] Sending [" << (ack->getFrameType() == 1 ? "ACK" : "NACK") << "] with number [" << (isnacked == true? ack->getSeq_Num():ack->getSeq_Num()) << "]" << std::endl;
        }
    }
}

//Function to process reciever window and slide it when messages come
void Node::processRecieverWindow(){
    //this bool tells me if i have moved multiple times in the reciever window not just once
    bool firsttime = true;
    //this is for output
    int i = 0;
    //this function is taken from the lecture
    while (receiverWindow[receiverWindowStart % (int)receiverWindowSize] != nullptr) {
        //this is the only extra part
        //here this is handling when i accepted an out of order frame and put it in the reciever
        //we send individual acks so i have to check and send an individual ack on it since that out of order frame didnt send an ack yet
        if(!firsttime)
        {
            //here i am sure that the second one is the out of order so im sending an ack on it
            //receiever window would look like 0 1 1 (when i have an out of order frame i accepted the second one and third one)
            //say the first one now came 1 1 1 (in my recieve message i am 100% sure i send ack on my expected frame so the other 2 were not acked so i need to ack them here
            //this all happened because we thought we have to send multiple nacks when out of order (confusion with some of what was said with TA hisa but he said if we handle it its okay)
            Message_Base *ack = new Message_Base("ack");
            ack->setACK_NACK_Number(1);
            ack->setFrameType(1);
            ack->setSeq_Num(receiverWindow[receiverWindowStart % (int)receiverWindowSize]->getSeq_Num());
            expectedSeqNum++;
            i++;
            //this is needed bsbb el mawdoo3 el out of order.
            ackedMessages[receiverWindow[receiverWindowStart % (int)receiverWindowSize]->getSeq_Num()%(int)receiverWindowSize] = false;
            outputFile << "At time[" << simTime() + (i+1)*processingDelay << "], Node[reciever] Sending [ack] with number [" << ack->getSeq_Num() << "]" << std::endl;
            sendDelayed(ack, (i+1)*processingDelay + transmissionDelay, "out_gate");
        }
       firsttime = false;
       //like i said here i know its acked so just remove it from reciever window (this is lectures function)
            receiverWindow[receiverWindowStart % (int)receiverWindowSize] = nullptr;
           // Move to the next message
           receiverWindowStart++;
       }
}

//This function handles ACKS (like actually i am acked here 5alas)
void Node::handleAcknowledgment(int ackNum)
{
    //Here i am 100% certain i am acked so just putting it that i am acked
    int index = ackNum%(int)senderWindowSize;
    if (ackNum >= senderWindowStart && ackNum < senderWindowEnd) {
        ackedMessages[index] = true;
    }

    //this while loops moves to the sender window with all the messages that have been acked
    while (senderWindowStart < senderWindowEnd && ackedMessages[(int)senderWindowStart % (int)senderWindowSize]) {
        //just removing stuff in the window to put other things if needed
        senderWindow[senderWindowStart % (int)senderWindowSize] = nullptr;
        ackedMessages[senderWindowStart % (int)senderWindowSize] = false;
        senderWindowStart++;
    }
    //send new messages if there are (as we increased senderwindowstart there can be a message to send)
    sendMessages();
}

void Node::handleMessage(cMessage *msg)
{
    //if i am a self message this means timeout happened!
    if(msg->isSelfMessage())
    {
        int index = std::stoi(msg->getName());
        // Resend the message
        //will only resend the message if this timeout happened within the senderwindow frame (every message has a timeout)
        //only sending that specific message not entire frame
        if (index >= senderWindowStart && index < senderWindowEnd) {
            Message_Base* resend = senderWindow[index % (int)senderWindowSize]->dup();
            resend->setPayload(frameEncoding(resend->getPayload()).c_str());
            sendDelayed(resend, processingDelay + transmissionDelay, "out_gate");
            outputFile << "Time out event at time [" << simTime() << "], at Node[sender] for frame with seq_num=[" << index << "]." << std::endl;
        }
    }
    else
    {
        //this would be the first message to see if im sender or reciever
        if(strcmp(msg->getSenderModule()->getName(),"coordinator")==0)
        {
            if(strcmp(getName(),"node0"))
              {
                reciever = "node0";
                sender = "node1";
              }
            else{
                reciever = "node1";
                sender = "node0";
            }
            readMessage(); //read everything from file
            sendMessages(); //start selective repeat
        }

        //here i am sender!
        if( strcmp(getName(),sender.c_str())==0 && !(strcmp(msg->getSenderModule()->getName(),"coordinator")==0))
        {
            //ill only get acks or nacks in sender
            Message_Base *ack = check_and_cast<Message_Base *>(msg);
             if (ack->getFrameType() == 1) {  //1 for ack
                 int ackNum = ack->getSeq_Num();
                 outputFile << "At time[" << simTime() << "], Node[sender] Recieved [" << (ack->getFrameType() == 1 ? "ACK" : "NACK") << "] with number [" << ack->getSeq_Num() << "]" << std::endl;
                 handleAcknowledgment(ackNum); //functions handles only acks (1)
             }
             if (ack->getFrameType() == 2) {  //2 for nack
                 int ackNum = ack->getSeq_Num();
                 outputFile << "At time[" << simTime() << "], Node[sender] Recieved [" << (ack->getFrameType() == 1 ? "ACK" : "NACK") << "] with number [" << ack->getSeq_Num() << "]" << std::endl;
                 //Document says if we get nack we send again but its totally correct this time
                 if (ackNum >= senderWindowStart && ackNum < senderWindowEnd) {
                     outputFile << "At time [" << simTime() << "], Node[Sender], Introducing channel error with code =[0000]. and msg = " << regularMessages[ackNum]<< std::endl;
                     Message_Base*resend = senderWindow[ackNum % (int)senderWindowSize]->dup();
                     resend->setPayload(frameEncoding(resend->getPayload()).c_str());
                     outputFile<<"At Time = ["<<simTime() + processingDelay <<"]  Node[Sender][Sent] Frame with seq_num = " << resend->getSeq_Num() << " and payload = [" << resend->getPayload() << "] and trailer = "<< resend->getChecksum() << " and Modified[-1], Lost[no], Duplicate[0], Delay[0]"<<endl;
                     sendDelayed(resend, processingDelay + transmissionDelay, "out_gate");
                 }
             }

        }

        //here im reciever!
        if(strcmp(getName(),reciever.c_str())==0 && !(strcmp(msg->getSenderModule()->getName(),"coordinator")==0))
        {
            //see the data that comes
            Message_Base * data = check_and_cast<Message_Base*>(msg);
            if(data->getFrameType()==0)
            {
                receiveMessage(data);
                processRecieverWindow();
            }
        }
    }
}

void Node::readMessage()
{
    outputFile.open("output.txt");
    std::ifstream inputFile("../src/node.txt");

    if (inputFile.is_open()) {
        std::string line;
        while (getline(inputFile, line)) {
            size_t spacePos = line.find(' ');
            std::string numberString = line.substr(0, spacePos); //error
            std::string message = line.substr(spacePos + 1); //message

            errorMessages.push_back(numberString);
            regularMessages.push_back(message);
        }
        inputFile.close();
    }
}

void Node::applyErrorBehavior(Message_Base *mmsg, const std::string &errorCode, int i=1) {
    // Convert the error code string to a bitset for easy manipulation
    std::bitset<4> errorBits(errorCode);
    Message_Base *modifiedMsg;
    if (errorBits == 0b0000) {
        // No error
        mmsg->setPayload(frameEncoding(mmsg->getPayload()).c_str());
        sendDelayed(mmsg, i*(processingDelay) + transmissionDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[0], Delay[0]"<<endl;
    } else if (errorBits == 0b0001) {
        // Delay
        mmsg->setPayload(frameEncoding(mmsg->getPayload()).c_str());
        sendDelayed(mmsg, i*(processingDelay) + transmissionDelay + errorDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[0], Delay["<<errorDelay<<"]"<<endl;
    } else if (errorBits == 0b0010) {
        // Duplication
        mmsg->setPayload(frameEncoding(mmsg->getPayload()).c_str());
        sendDelayed(mmsg, i*(processingDelay) + transmissionDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[1], Delay[0]"<<endl;
        sendDelayed(mmsg->dup(), i*(processingDelay) + transmissionDelay + duplicationDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) + duplicationDelay <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[2], Delay[0]"<<endl;
    } else if (errorBits == 0b0011) {
        // Duplication with Delay
        mmsg->setPayload(frameEncoding(mmsg->getPayload()).c_str());
        sendDelayed(mmsg, i*(processingDelay) + transmissionDelay + errorDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[1], Delay["<<errorDelay<<"]"<<endl;
        sendDelayed(mmsg->dup(), i*(processingDelay) + transmissionDelay + errorDelay + duplicationDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) + duplicationDelay <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[no], Duplicate[2], Delay["<<errorDelay<<"]"<<endl;

    } else if (errorBits.to_ulong() >= 0b0100 && errorBits.to_ulong() <= 0b0111) {
        // Loss
        // Do not send the message (Message lost)
        EV << "Message lost due to error condition\n";
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << mmsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[-1], Lost[Yes], Duplicate[0], Delay[0]"<<endl;

    } else if (errorBits == 0b1000) {
        // Modification
        modifiedMsg = mmsg->dup(); // Duplicate the message
        modifyMessage(modifiedMsg); // Modify the duplicate
        modifiedMsg->setPayload(frameEncoding(modifiedMsg->getPayload()).c_str());
        sendDelayed(modifiedMsg, i*(processingDelay) + transmissionDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[0], Delay[0]"<<endl;
    } else if (errorBits == 0b1001) {
        // Modification with Delay
        modifiedMsg = mmsg->dup(); // Duplicate the message
        modifyMessage(modifiedMsg); // Modify the duplicate
        modifiedMsg->setPayload(frameEncoding(modifiedMsg->getPayload()).c_str());
        sendDelayed(modifiedMsg, i*(processingDelay) + transmissionDelay + errorDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[0], Delay["<<errorDelay<<"]"<<endl;
    } else if (errorBits == 0b1010) {
        // Modification and Duplication
        modifiedMsg = mmsg->dup(); // Duplicate the message
        modifyMessage(modifiedMsg); // Modify the duplicate
        modifiedMsg->setPayload(frameEncoding(modifiedMsg->getPayload()).c_str());
        sendDelayed(modifiedMsg, i*(processingDelay) + transmissionDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[1], Delay[0]"<<endl;
        Message_Base *dupMsg = modifiedMsg->dup(); // Duplicate the modified message
        sendDelayed(dupMsg, i*(processingDelay) + transmissionDelay + duplicationDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) + duplicationDelay <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[2], Delay[0]"<<endl;
    } else if (errorBits == 0b1011) {
        // Modification, Duplication, and Delay
        modifiedMsg = mmsg->dup(); // Duplicate the message
        modifyMessage(modifiedMsg); // Modify the duplicate
        modifiedMsg->setPayload(frameEncoding(modifiedMsg->getPayload()).c_str());
        sendDelayed(modifiedMsg, i*(processingDelay) + transmissionDelay + errorDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[1],  Delay["<<errorDelay<<"]"<<endl;
        Message_Base *dupMsg = modifiedMsg->dup(); // Duplicate the modified message
        sendDelayed(dupMsg, i*(processingDelay) + transmissionDelay + errorDelay + duplicationDelay, "out_gate");
        outputFile<<"At Time = ["<<simTime() + i*(processingDelay) + duplicationDelay <<"]  Node[Sender][Sent] Frame with seq_num = " << mmsg->getSeq_Num() << " and payload = [" << modifiedMsg->getPayload() << "] and trailer = "<< mmsg->getChecksum() << " and Modified[1], Lost[no], Duplicate[2],  Delay["<<errorDelay<<"]"<<endl;
    }

}

void Node::modifyMessage(Message_Base *mmsg) {
    // Implement the logic to modify a bit in the message's payload or checksum
    std::string payload = mmsg->getPayload();
    if (!payload.empty()) {
        payload[0] ^= 1; // Example: Flip the first bit of the payload
        mmsg->setPayload(payload.c_str());
    }
    EV<<payload;
}

struct TimedLine {
    double time;
    std::string line;
};

// Function to parse the time from the beginning of each line
double parseTime(const std::string& line) {
    std::size_t startPos = line.find("[") + 1;
    std::size_t endPos = line.find("]");
    std::string timeStr = line.substr(startPos, endPos - startPos);
    return std::stod(timeStr);
}


Node::~Node() {
      std::string filePath = "output.txt";  // Your file path
      std::ifstream inputFile(filePath);
      std::string currentLine;
      std::vector<TimedLine> lines;

      // Read the file and extract timestamps
      while (getline(inputFile, currentLine)) {
          double time = parseTime(currentLine);
          lines.push_back({time, currentLine});
      }
      inputFile.close();

      // Sort the entries by time
      std::sort(lines.begin(), lines.end(), [](const TimedLine& a, const TimedLine& b) {
          return a.time < b.time;
      });

      // Write the sorted entries back to a new file
      std::ofstream outputFile("sorted_output.txt");
      for (const auto& line : lines) {
          outputFile << line.line << std::endl;
      }
      outputFile.close();

}
