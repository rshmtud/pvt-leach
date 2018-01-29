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

#include <Node.h>
#include <Sink.h>
#include<RANDOM.H>
#include<custMsg_m.h>
#include<algorithm>
#include<string>
#include<stdlib.h>
#include<cUtility.h>

Define_Module(Node);

Node::Node() {
    // TODO Auto-generated constructor stub
    this->X = 0;
    this->Y = 0;
    batteryPower = 0.5;
    this->G = 0.0;
    this->type = 'N'; //Normal Node

    //Sleep time
    sleepTime = 0.10;

    Efs = 10 * 0.000000000001;
    Emp = 0.0013 * 0.000000000001;
    Do = sqrt(Efs / Emp);

    CHETx = 0.0;
    CHERx = 0.0;
    NETX = 0.0; //Node ETX
    NERX = 0.0; //Node ERX

    ETX = 50 * 0.000000001;
    ERX = 50 * 0.000000001;

    EDA = 5 * 0.000000001;

    CHIndex = 0;
    roundInterval = 1.0;

    thresholdEnergy = 0;

    noDataSentToCH = 0;
    noDataSentToSink = 0;
}

Node::~Node() {
    // TODO Auto-generated destructor stub

//    dataQueue->clear();
//    delete dataQueue;

}

void Node::initialize() {
    dataQueue = new cQueue;  //Queue for incoming message
    chDataQueue = new cQueue;

    wakeup = CreateCustMsg("Wakeup");
    noOfWirelessNode = getParentModule()->par("noOfWirelessNode");
    clusterHeadPercentage = getParentModule()->par("clusterHeadPercentage");

    this->netSizeX = getParentModule()->par("netSizeX");
    this->netSizeY = getParentModule()->par("netSizeY");

    SetCoordinate();

    //Set Initial Cluster head and cluster
    if (simTime().dbl() == 0.0 && getIndex() == (noOfWirelessNode - 1)) {
        //endSimulation();
        ClusterHeadSelection(0);
        ClusterFormation(0);
    }

    /***********************************************************************
     * Initial schedule for each node
     * *********************************************************************/
    scheduleAt(
            simTime().dbl()
                    + ((double) noOfWirelessNode - getIndex())
                            / (double) noOfWirelessNode, wakeup);
}

void Node::ThresholdCheck() {
    //If network remaining energy is less than or equal to threshold enerngy
    //then end simulation
    //int noOfNodeDied = getParentModule()->par("noOfNodeDied");

    double netRemainingEnergy = (double) (getParentModule()->par(
            "totalRemainingEnergy"));

    int roundNo = getParentModule()->par("roundNumber");

    if (netRemainingEnergy <= thresholdEnergy && roundNo > 0) {

        getParentModule()->par("allNodeDieRound") = roundNo - 1;
        getParentModule()->par("noOfNodeDied") = noOfWirelessNode;

        int noOfNodeDied = getParentModule()->par("noOfNodeDied");
        WriteDeadNodeHistory(noOfNodeDied, roundNumber -1 );
        WriteNetworkEnergyHistory(roundNumber - 1);
        int noOfCH = getParentModule()->par("noOfCH");
        CountCH(roundNumber - 1, noOfCH);
        CountSinkPacket(roundNumber -1);
        EV<<"netRemainingEnergy: " << netRemainingEnergy <<"roundNo: " << roundNo <<endl;
        endSimulation();
    }
}

void Node::handleMessage(cMessage *msg) {

    int networkStatus = getParentModule()->par("networkStatus");
    double lastRoundTime = getParentModule()->par("lastRoundTime");
    roundNumber = getParentModule()->par("roundNumber");
    int noOfNodeDied = getParentModule()->par("noOfNodeDied");


    //If network remaining energy is less than or equal to threshold enerngy
    //then end simulation

    //noOfNodeDied = ThresholdCheck(noOfNodeDied);
    /*Casting incomming msg to custMsg(Customize message)*/
    custMsg *inMsg = check_and_cast<custMsg *>(msg);

    //Handle incoming data message
    if (strcmp("DataMsg", inMsg->getFullName()) == 0) {

        //Power consumtion for incomming message
        if(this->type == 'C' && this->batteryPower >= 0)
        {
            //For Cluster head
            this->batteryPower = this->batteryPower - this->CHERx;

            if(this->batteryPower <= 0)
            {
                noOfNodeDied++;
                getParentModule()->par("noOfNodeDied") = noOfNodeDied;

                if (noOfNodeDied == 1) {
                    getParentModule()->par("fstNodeDieRound") = roundNumber;
                }

                if (noOfNodeDied == 10) {
                    getParentModule()->par("tenNodeDieRound") = roundNumber;
                }

                if (noOfNodeDied == noOfWirelessNode) {
                    getParentModule()->par("allNodeDieRound") = roundNumber;
                }
            }
        }

        inMsg->setPacketReachTime(simTime().dbl());
        chDataQueue->insert(inMsg);
    }

    //Initial setup
    if (roundNumber <= 0 && getIndex() == noOfWirelessNode - 1) {
        //EV << "Initial Setting round time and round number" << endl;
        roundNumber = roundNumber + 1;
        lastRoundTime = simTime().dbl();
        getParentModule()->par("roundNumber") = roundNumber;
        getParentModule()->par("lastRoundTime") = lastRoundTime;
        getParentModule()->par("networkStatus") = 2; //Means now nodes will send data to sink.
        //endSimulation();
    }

    //Send data to CH
    if (networkStatus == 2) {
        if (this->type == 'N') {
            SendDataToCH();
        }

        if(getIndex() == noOfWirelessNode - 1)
        {
            //Means all data sent to CH and CH will send data to Sink
            getParentModule()->par("networkStatus") = 3;
        }
    }

    //Send data to Sink
    if (networkStatus == 3) {
        if (this->type == 'C') {
            SendDataToSink();
        }

        if(getIndex() == noOfWirelessNode - 1)
        {
            //Means all data sent to Sink, ready for setup round
            getParentModule()->par("networkStatus") = 1;
            //EV<<"All data sent to CH in round : "<<roundNumber<<endl;
            //endSimulation();
        }
    }


    //Setup state
    if (roundNumber > 0
            && simTime().dbl() >= lastRoundTime + this->roundInterval
            && getIndex() == noOfWirelessNode - 1
           &&networkStatus == 1
            ) {

        roundNumber = roundNumber + 1;
        lastRoundTime = simTime().dbl();
        getParentModule()->par("roundNumber") = roundNumber;
        getParentModule()->par("lastRoundTime") = lastRoundTime;
        ClusterHeadSelection(roundNumber);
        ClusterFormation(roundNumber);
        getParentModule()->par("networkStatus") = 2; //Means now nodes will send data to sink.
    }

    if (msg->isSelfMessage()) {
        custMsg *dataMessage = CreateCustMsg("DataMsg");
        dataQueue->insert(dataMessage);
        scheduleAt(simTime().dbl() + sleepTime, msg->dup()); //Schedule after 0.5 second
    }
}
void Node::finish() {

    WriteOneTenAllNodeDeadHistory();

    /*    cancelAndDelete(wakeup);
     cancelAndDelete(data);
     delete receiverNode;
     delete senderNode;
     delete receiverNode;
     delete tempTargetModule;
     delete tempBaseModule;*/

}

void Node::SetCoordinate() {

    int corX = 0;
    int corY = 0;
    int startAfterX = 5;
    int endBeforeX = netSizeX-5;
    int startAfterY = 20;
    int endBeforeY = netSizeX-5;

    //Take Rand value >10 and <=750
    do {
        corX = intrand(netSizeX); //old = 800
    } while (corX <= startAfterX || corX >= endBeforeX); //Old 10,750

    do {
        corY = intrand(netSizeY);
    } while (corY <= startAfterY || corY >= endBeforeY); //old 70, 550

    this->X = corY;
    this->Y = corY;

    getDisplayString().setTagArg("p", 0, corX);
    getDisplayString().setTagArg("p", 1, corY);
}

void Node::ClusterHeadSelection(int roundNo) {

    //ResetG(roundNo);

    //Cound no of CH before reset
    int noOfCH = 0;
    noOfCH = getParentModule()->par("noOfCH");
    //Calculate noOf dead node before creating new node cluster
    getParentModule()->par("noOfCluster") = 0; //Reset number of cluster
    getParentModule()->par("noOfCH") = 0; //Reset number of cluster head

    int noOfNodeDied = getParentModule()->par("noOfNodeDied");

    ThresholdCheck();

    WriteDeadNodeHistory(noOfNodeDied, roundNo - 1);
    WriteNetworkEnergyHistory(roundNo - 1);
    CountCH(roundNo - 1, noOfCH);
    CountSinkPacket(roundNo - 1);
    CountThroughput(roundNo - 1);

    //Reset parameter for each round
    ResetParam();

/*    double netRemainingEnergy = (double) (getParentModule()->par(
            "totalRemainingEnergy"));
    if (netRemainingEnergy <= thresholdEnergy) {
        endSimulation();
    }*/

    double temp_rand;
    double p = clusterHeadPercentage;
    int roundNumber = roundNo;
    double distance = 0.0;
    int noOfCluster = 0;
    noOfCH = 0;


    if (roundNo % (int) (round)(1 / p) == 0 && roundNo != 0) {
        //if (roundNo % 5 == 0) {
        //EV<<"Resetting node: " <<endl;
        //endSimulation();
        ResetG(roundNo);
    }


    //Initially reset all node as normal node
    for (int i = 0; i < noOfWirelessNode; i++) {
        tempModule = getParentModule()->getSubmodule("node", i);
        tempNode = check_and_cast<Node *>(tempModule);
        tempNode->type = 'N';
        tempNode->CHIndex = 0;
    }

    //Cluster head selection: Choose CH from all nodes
    for (int i = 0; i < noOfWirelessNode; i++) {
        //EV << "Id : " << i << endl;
        tempModule = getParentModule()->getSubmodule("node", i);
        tempNode = check_and_cast<Node *>(tempModule);

        EV<<"Round: " <<roundNo <<" Node: " <<i<<" G: " <<tempNode->G <<endl;


        //EV<<"G: " <<tempNode->G<<endl;
//        if(roundNo == 160 && i>48)
//        {
//            endSimulation();
//        }


        if (tempNode->batteryPower > 0.0) {
            temp_rand = dblrand(); //Generate a random variable 0 to 1

            if (tempNode->G <= 0) {

                //if(temp_rand<= (p/(1-p*mod(r,round(1/p)))))  % Our recharch point

//


                double rechargeValue = p
                        / (1 - p * (roundNumber % (int) (round)(1 / p)));


                if (temp_rand <= rechargeValue) {

                    noOfCH = getParentModule()->par("noOfCH");
                    noOfCH = noOfCH + 1;
                    getParentModule()->par("noOfCH") = noOfCH;
                    //EV << "CH no: " << noOfCH << endl;
                    tempNode->type = 'C'; //Cluster node

                    //Populate global CH list
                    //String conversion Begin
                    char buffer[33];
                    itoa(i, buffer, 10);
                    std::string strCH(buffer);

                    if (noOfCH <= 1) {
                        getParentModule()->par("lstCH") = strCH;
                    } else {

                        std::string prevList = getParentModule()->par("lstCH");
                        getParentModule()->par("lstCH") = prevList + ","
                                + strCH;

                    }

                    tempNode->G = round(1 / p) - 1;

                    distance = CalculateDistanceToBS(i);

                    tempNode->distanceToBS = distance;

                    noOfCluster = getParentModule()->par("noOfCluster");
                    getParentModule()->par("noOfCluster") = noOfCluster + 1;

                    //EV<<"DO: " << tempNode->Do <<endl;
                    //EV<<"distanceToBS" << tempNode->distanceToBS <<endl;

                    if (tempNode->distanceToBS > tempNode->Do) {
                        tempNode->CHETx = (tempNode->ETX + tempNode->EDA)
                                * (4000)
                                + tempNode->Emp * 4000
                                        * (distance * distance * distance
                                                * distance);

                        //EV<<"CHETx" << tempNode->CHETx <<endl;
                        //endSimulation();
                    }
                    if (tempNode->distanceToBS <= tempNode->Do) {
                        tempNode->CHETx = (tempNode->ETX + tempNode->EDA)
                                * (4000)
                                + tempNode->Efs * 4000 * (distance * distance);
                    }

                    //Energy loss of CH to receive data
                    tempNode->CHERx = (ERX + EDA) * 4000;
                }
            }

        }
    }

//    if(roundNumber == 10)
//       {
//           //EV<<"rechargeValue: " <<rechargeValue <<endl;
//           endSimulation();
//       }
}

//Calculate node distance from Sink
int Node::CalculateDistanceToBS(int senderIndex) {

    int senderX, senderY, receiverX, receiverY, baseDistance;

//    cModule *senderNode;
//    cModule *receiverNode;
//    Node *tempTargetModule;

    senderNode = getParentModule()->getSubmodule("node", senderIndex);
    receiverNode = getParentModule()->getSubmodule("sink"); //Base station node
    //getParentModule()->getSubmodule("node", receiverIndex);

    tempTargetModule = check_and_cast<Node *>(senderNode);
    senderX = tempTargetModule->X;
    senderY = tempTargetModule->Y;

    tempBaseModule = check_and_cast<Sink *>(receiverNode);
    receiverX = tempBaseModule->X;
    receiverY = tempBaseModule->Y;

    baseDistance = (int) sqrt(
            (receiverX - senderX) * (receiverX - senderX)
                    + (receiverY - senderY) * (receiverY - senderY));

    return baseDistance;
}

//        EV << "Testing--Start" << endl;
//        EV << "Output-01: " << i << endl;
//        EV << "Output-02: " << rechargeValue << endl;
//        EV << "Testing--End" << endl;
//        endSimulation();

void Node::ClusterFormation(int roundNo) {

    int noOfCluster = getParentModule()->par("noOfCluster");

    for (int i = 0; i < noOfWirelessNode; i++) {
        tempModule = getParentModule()->getSubmodule("node", i);
        tempNode = check_and_cast<Node *>(tempModule);

        //if(noOfCluster-1 >= 1)
        int minDistance = 32000;    //infinity
        int minDistToCluster = 0;

        cModule *tempCHModule;
        Node *tempCHNode;
        int tempDistance = 0;

        if (tempNode->type == 'N' && tempNode->batteryPower > 0) {
            //for each CH
            for (int ch = 0; ch < noOfWirelessNode; ch++) {
                tempCHModule = getParentModule()->getSubmodule("node", ch);
                tempCHNode = check_and_cast<Node *>(tempCHModule);
                if (tempCHNode->type == 'C') {
                    //EV << "CH: " << ch << endl;
                    tempDistance = std::min(minDistance,
                            CalculateDistance(i, ch));

                    if (tempDistance < minDistance) {
                        minDistance = tempDistance;
                        tempNode->CHIndex = ch;
                        //min_dis_cluster=c;
                    }

                } else {
                    continue;
                }
            }

            //EV << "Node index: " << i << " CH: " << tempNode->CHIndex << endl;
        }


        if(getIndex() == 0)
        {
            EV<<"distance: " <<minDistance <<endl;
        }

        //skip the rest of the calculation for cluster head
         if(tempNode->type == 'C')
         {
             continue;
         }


        if (minDistance > Do) {
            tempNode->NETX = ETX * (4000)
                    + Emp * 4000
                            * (minDistance * minDistance * minDistance
                                    * minDistance);
        }

        if (minDistance <= Do) {
            tempNode->NETX = ETX * (4000)
                    + Efs * 4000 * (minDistance * minDistance);
        }

        //must remove
//        if(tempNode->NETX >= 0.5)
//        {
//            EV<<"Node: " << i <<endl;
//            EV<<"Do " << Do <<endl;
//            EV<<"ETX " << NETX << endl;
//            EV<<"Efs " << Efs << endl;
//            EV<<"Distance to BS: " << minDistance << endl;
//            EV<<"ETX for this node: " <<tempNode->NETX << endl;
//
//            endSimulation();
//        }
    }
}

custMsg* Node::CreateCustMsg(const char *name) {

//    if(strcmp("Beacon", name) == 0 || strcmp("Preamble", name) == 0 || strcmp("ACK", name) == 0 )
//    {
//        this->countControlPkt++;
//    }
//
    custMsg *createMsg = new custMsg(name);
    createMsg->setPacketGenerateTime(simTime().dbl());
    createMsg->setPacketReachTime(simTime().dbl());

    createMsg->setSourceId(getIndex()); // Set source node's index for the message

//    int curMsgId = getParentModule()->par("gMessageId");
//    curMsgId = curMsgId + 1;
//    createMsg->setMessageId(curMsgId);
//    getParentModule()->par("gMessageId") = curMsgId;

    return createMsg;
}

int Node::CalculateDistance(int senderIndex, int receiverIndex) {

    int senderX, senderY, receiverX, receiverY, baseDistance;

    cModule *senderNode;
    cModule *receiverNode;
    Node *tempTargetModule;

    senderNode = getParentModule()->getSubmodule("node", senderIndex);
    receiverNode = getParentModule()->getSubmodule("node", receiverIndex);

    tempTargetModule = check_and_cast<Node *>(senderNode);
    senderX = tempTargetModule->X;
    senderY = tempTargetModule->Y;

    tempTargetModule = check_and_cast<Node *>(receiverNode);
    receiverX = tempTargetModule->X;
    receiverY = tempTargetModule->Y;

    baseDistance = (int) sqrt(
            (receiverX - senderX) * (receiverX - senderX)
                    + (receiverY - senderY) * (receiverY - senderY));

    return baseDistance;
}

//Not used
void Node::TempDataSendToCH() {
    int noOfCH = getParentModule()->par("noOfCH");
    EV << "DataSendToCH, No of CH: " << noOfCH << endl;

    std::string lstCH = getParentModule()->par("lstCH");

    EV << "CH Lists: " << lstCH << endl;
    cUtility *objcUtility = new cUtility;
    std::vector<std::string> result = objcUtility->split(lstCH, ',');
    std::vector<int> vlstCH = objcUtility->convertToInt(result);

    for (int i = 0; i < vlstCH.size(); i++) {
        EV << "Converted CH: " << vlstCH[i] << endl;
    }

    //endSimulation();
    for (int ch = 0; ch < vlstCH.size(); ch++) {
        int destNodeIndex = vlstCH[ch];
        tempDestModule = getParentModule()->getSubmodule("node", destNodeIndex);
        tempDestNode = check_and_cast<Node *>(tempDestModule);

        for (int i = 0; i < noOfWirelessNode; i++) {
            tempSrcModule = getParentModule()->getSubmodule("node", i);
            tempSrcNode = check_and_cast<Node *>(tempSrcModule);
            if (tempSrcNode->CHIndex == destNodeIndex
                    && tempSrcNode->type == 'N') {
                EV << "Node index: " << i << "CH index: " << destNodeIndex
                          << endl;

                int queueLen = tempSrcNode->dataQueue->length();
                if (queueLen > 0) {
                    cObject *cObj = tempSrcNode->dataQueue->pop();
                    custMsg *qMsg = check_and_cast<custMsg *>(cObj);
                    //sendDirect(qMsg, baseModule, "radioIn");
                }

                //endSimulation();
//                cObject *cObj = dataQueue->pop();
//                custMsg *qMsg = check_and_cast<custMsg *>(cObj);
//                sendDirect(qMsg, baseModule, "radioIn");

            }
        }

    }

   // endSimulation();

}



void Node::TempDataSendToSink() {
}

void Node::SendDataToCH() {

    int noOfNodeDied = getParentModule()->par("noOfNodeDied");
    int destNodeIndex = this->CHIndex;
    tempDestModule = getParentModule()->getSubmodule("node", destNodeIndex);

    int queueLen = dataQueue->length();

    if (queueLen > 0) {
        cObject *cObj = dataQueue->pop();
        custMsg *qMsg = check_and_cast<custMsg *>(cObj);

        if (this->batteryPower > 0) {
            this->batteryPower = this->batteryPower - this->NETX;

            if (this->batteryPower <= 0) {
                noOfNodeDied++;
                getParentModule()->par("noOfNodeDied") = noOfNodeDied;

                if (noOfNodeDied == 1) {
                    getParentModule()->par("fstNodeDieRound") = roundNumber;
                }

                if (noOfNodeDied == 10) {
                    getParentModule()->par("tenNodeDieRound") = roundNumber;
                }

                if (noOfNodeDied == noOfWirelessNode) {
                    getParentModule()->par("allNodeDieRound") = roundNumber;
                }

                deadTime = simTime().dbl();
            }
        }

        //Added to calculate throughput
        noDataSentToCH = getParentModule()->par("noDataSentToCH");
        getParentModule()->par("noDataSentToCH") = noDataSentToCH + 1;

        sendDirect(qMsg, tempDestModule, "radioIn");
    }

}

void Node::SendDataToSink() {

    int noOfNodeDied = getParentModule()->par("noOfNodeDied");
    sinkModule = getParentModule()->getSubmodule("sink");

    int queueLen = chDataQueue->length();

    int noPacketSentToSink = getParentModule()->par("noPacketSentToSink");
    getParentModule()->par("noPacketSentToSink") = noPacketSentToSink + queueLen;

    while (queueLen > 0) {
        cObject *cObj = chDataQueue->pop();
        custMsg *qMsg = check_and_cast<custMsg *>(cObj);
        sendDirect(qMsg, sinkModule, "radioIn");
        queueLen--;
    }

    //chDataQueue->empty();
    //->clear(); //Clear after sending data.

    //added to calculate throughput
    noDataSentToSink = getParentModule()->par("noDataSentToCH");
    getParentModule()->par("noDataSentToSink") = noDataSentToSink;

    if (this->batteryPower > 0) {
        this->batteryPower = this->batteryPower - this->CHETx;

        if (this->batteryPower <= 0) {
            noOfNodeDied++;
            getParentModule()->par("noOfNodeDied") = noOfNodeDied;
            deadTime = simTime().dbl();

            if (noOfNodeDied == 1) {
                getParentModule()->par("fstNodeDieRound") = roundNumber;
            }

            if (noOfNodeDied == 10) {
                getParentModule()->par("tenNodeDieRound") = roundNumber;
            }

            if (noOfNodeDied == noOfWirelessNode) {
                getParentModule()->par("allNodeDieRound") = roundNumber;
            }
        }
    }

}

void Node::WriteDeadNodeHistory(int noOfDeadNode, int roundNumber) {

    const char* filename = "02-RoundVsDeadNode.csv";
    const char* filenameAliveNode = "03-RoundVsAliveNode.csv";

    FILE *file;
    FILE *fileAliveNode;

    if (roundNumber < 1) {
          file = fopen(filename, "w+");
          fileAliveNode = fopen(filenameAliveNode, "w+");
          fprintf(file, "Round Number, Dead Node\n");
          fprintf(fileAliveNode, "Round Number, Alive Node\n");
    } else {
        //Append or update mode
        file = fopen(filename, "a+");
        fileAliveNode = fopen(filenameAliveNode, "a+");
    }

    //FILE *file = fopen(filename, "w+");

    fprintf(file, "%d,%d\n", roundNumber, noOfDeadNode);
    fclose(file);

    fprintf(fileAliveNode, "%d,%d\n", roundNumber, noOfWirelessNode - noOfDeadNode);
    fclose(fileAliveNode);

}

void Node::WriteOneTenAllNodeDeadHistory() {
    const char* filename = "01-1st10thAllDie.csv";
    //const char* filenameAliveNode = "03-RoundVsAliveNode.csv";

    FILE *file;
    FILE *fileAliveNode;

    file = fopen(filename, "w+");
    fprintf(file, "Dead Node, Round Number\n");

    int fstNodeDieRound = getParentModule()->par("fstNodeDieRound");
    fprintf(file, "%d,%d\n", 1, fstNodeDieRound);

    int tenNodeDieRound = getParentModule()->par("tenNodeDieRound");
    fprintf(file, "%d,%d\n", 10, tenNodeDieRound);

    int allNodeDieRound = getParentModule()->par("allNodeDieRound");;
    fprintf(file, "%d,%d\n", 50, allNodeDieRound);

    fclose(file);
}

void Node::WriteNetworkEnergyHistory(int roundNumber) {

    //if(roundNumber <1 ) return;
    double initialBatteryPower = 0.5;
    double netInitialEnergy = initialBatteryPower * (double)noOfWirelessNode;
    double netRemainingEnergy = 0.0;
    double netConsumptionEnergy = 0.0;

    //EV<<"Net initial energy: " << netInitialEnergy <<endl;
    //endSimulation();

    for (int i = 0; i < noOfWirelessNode; i++) {
        calModule = getParentModule()->getSubmodule("node", i);
        calNode = check_and_cast<Node *>(calModule);
        netRemainingEnergy = netRemainingEnergy + calNode->batteryPower;
    }

    if(netRemainingEnergy <= 0)
    {
        netRemainingEnergy = 0.0;
    }

    getParentModule()->par("totalRemainingEnergy") = netRemainingEnergy;
    getParentModule()->par("avgRemainingEnergy") = netRemainingEnergy / (double)noOfWirelessNode;

    netConsumptionEnergy = netInitialEnergy - netRemainingEnergy;

    const char* filenameRemainingEnergy = "04-RoundVsRemainingEnergy.csv";
    const char* filenameEnergyConsumption = "05-RoundVsEnergyConsumption.csv";

    FILE *fileRemainingEnergy;
    FILE *fileEnergyConsumption;

    if (roundNumber < 1) {
        fileRemainingEnergy = fopen(filenameRemainingEnergy, "w+");
        fileEnergyConsumption = fopen(filenameEnergyConsumption, "w+");
        fprintf(fileRemainingEnergy, "Round Number, Network Remaining Energy\n");
        fprintf(fileEnergyConsumption, "Round Number, Total Energy Consumption\n");
    } else {
        //Append or update mode
        fileRemainingEnergy = fopen(filenameRemainingEnergy, "a+");
        fileEnergyConsumption = fopen(filenameEnergyConsumption, "a+");
    }


    fprintf(fileRemainingEnergy, "%d,%lf\n", roundNumber, netRemainingEnergy);
    fclose(fileRemainingEnergy);

    fprintf(fileEnergyConsumption, "%d,%lf\n", roundNumber,
            netConsumptionEnergy);
    fclose(fileEnergyConsumption);
    //FILE *file = fopen(filename, "w+");

}

void Node::CountSinkPacket(int roundNumber) {
    if(roundNumber <= 0 )
    {
        return;
    }

    int noPacketSentToSink = getParentModule()->par("noPacketSentToSink");
        //getParentModule()->par("noPacketSentToSink") = noPacketSentToSink + queueLen;

    int sinkPackets = 0;

    calSinkModule = getParentModule()->getSubmodule("sink");
    calSinkNode = check_and_cast<Sink *>(calSinkModule);
    sinkPackets = calSinkNode->noDataInSink;
    //noDataInSink
            //calSinkNode->sinkDataQueue->getLength();

    //sinkPackets = ;

    const char* filenameSinkPackets = "06-RoundVsSinkPackets.csv";
    FILE *fileSinkPackets;

    if (roundNumber < 1) {
        fileSinkPackets = fopen(filenameSinkPackets, "w+");
        fprintf(fileSinkPackets, "Round Number, Number of packets in Sink\n");
    } else {
        //Append or update mode
        fileSinkPackets = fopen(filenameSinkPackets, "a+");
    }


    fprintf(fileSinkPackets, "%d,%d\n", roundNumber, noPacketSentToSink);
    fclose(fileSinkPackets);

}

void Node::ResetG(int roundNo) {

    bool isResetRequired = true;
/*    for (int i = 0; i < noOfWirelessNode; i++) {
         tempModule = getParentModule()->getSubmodule("node", i);
         tempNode = check_and_cast<Node *>(tempModule);
         if(tempNode->G <= 0)
         {
             isResetRequired = false;
         }
         //tempNode->type = 'N';
         //tempNode->CHIndex = 0;
     }*/

    if (isResetRequired == 1) {
        for (int i = 0; i < noOfWirelessNode; i++) {
            tempModule = getParentModule()->getSubmodule("node", i);
            tempNode = check_and_cast<Node *>(tempModule);
            tempNode->G = 0;
            EV<<"Reset G: " << tempNode->G <<endl;
            //endSimulation();
        }
    }

//    if (roundNo == 160) {
//        endSimulation();
//    }

}

void Node::CountCH(int roundNumber, int noOfCH) {

    const char* filenameCHVsRound= "07-RoundVsCH.csv";
    //const char* filenameEnergyConsumption = "05-RoundVsEnergyConsumption.csv";

    FILE *fileCHVsRound;
    //FILE *fileEnergyConsumption;

    if (roundNumber < 1) {
        fileCHVsRound = fopen(filenameCHVsRound, "w+");
        fprintf(fileCHVsRound,
                "Round Number, No of CH\n");
    } else {
        //Append or update mode
        fileCHVsRound = fopen(filenameCHVsRound, "a+");
    }

    fprintf(fileCHVsRound, "%d,%d\n", roundNumber, noOfCH);
    fclose(fileCHVsRound);
}


void Node::ResetParam() {
    noDataSentToCH = 0;
    noDataSentToSink = 0;

    getParentModule()->par("noDataSentToCH") = 0;
    getParentModule()->par("noDataSentToSink") = 0;
}


void Node::CountThroughput(int roundNumber) {

    noDataSentToSink = getParentModule()->par("noDataSentToSink");
    noDataInSink = getParentModule()->par("noDataInSink");
    getParentModule()->par("noDataInSink") = noDataInSink + noDataSentToSink;
    int noDataInSinkAtPresent = getParentModule()->par("noDataInSink");

    const char* filenameThroughput = "08-RoundVsThroughput.csv";
    const char* filenameNoDataInSink = "09-RoundVsNoDataInSink.csv";

    FILE *fileThroughput;
    FILE *fileNoDataInSink;

    if (roundNumber < 1) {
        fileThroughput = fopen(filenameThroughput, "w+");
        fileNoDataInSink = fopen(filenameNoDataInSink, "w+");
        fprintf(fileThroughput,
                "Round Number, Throughput\n");

        fprintf(fileNoDataInSink,
                        "Round Number, No data in Sink\n");

    } else {
        //Append or update mode
        fileThroughput = fopen(filenameThroughput, "a+");
        fileNoDataInSink = fopen(filenameNoDataInSink, "a+");
    }

    fprintf(fileThroughput, "%d,%d\n", roundNumber, noDataSentToSink);
    fprintf(fileNoDataInSink, "%d,%d\n", roundNumber, noDataInSinkAtPresent);

    fclose(fileThroughput);
    fclose(fileNoDataInSink);
}
