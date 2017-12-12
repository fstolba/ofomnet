#include <omnetpp.h>
#include <OFA_controller.h>

#include "GenericAppMsg_m.h"
#include "openflow.h"
#include "Open_Flow_Message_m.h"
#include "Hub.h"
#include "OF_Wrapper.h"
#include "OFP_Packet_Out_m.h"
#include "TCPCommand_m.h"
#include "Switch.h"
#include "OFP_Flow_Mod_m.h"
#include "OFP_Features_Request_m.h"
#include "OFP_Features_Reply_m.h"
#include "Forwarding.h"
#include "TCPConnection.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "list"
#include <Ieee802Ctrl_m.h>

using namespace std;
using inet::TCPConnectInfo;
using inet::TCPSocket;
using inet::TCPCommand;
using inet::TcpCommandCode::TCP_C_SEND;
using inet::TCPDataTransferMode::TCP_TRANSFER_OBJECT;
using inet::L3Address;
using inet::L3AddressResolver;
using inet::EtherType;

Define_Module(OFA_controller);

OFA_controller::OFA_controller()
{


}

OFA_controller::~OFA_controller()
{

}

void OFA_controller::initialize()
{
    const char *address = par("address");
    int port = par("port");
    serviceTime = par("serviceTime");

    // TCP socket; listen on incoming connections
    socket1.setOutputGate(gate("tcpOut"));
    socket1.setDataTransferMode(TCP_TRANSFER_OBJECT);
    socket1.bind(address[0] ? L3AddressResolver().resolve(address) : L3Address(), port);

    socket1.listen();

    PacketInSignalId = registerSignal("PacketIn");
    connIDSignal = registerSignal("connID");


    busy = false;
}

TCPSocket *OFA_controller::findSocketFor(cMessage *msg)
{
    TCPCommand *ind = dynamic_cast<TCPCommand *>(msg->getControlInfo());
    if (!ind)
        throw omnetpp::cRuntimeError("TCPSocketMap: findSocketFor(): no TCPCommand control info in message (not from TCP?)");
    int connId = ind->getConnId();
    SocketMap::iterator i = socketMap.find(connId);
    ASSERT(i==socketMap.end() || i->first==i->second->getConnectionId());
    return (i==socketMap.end()) ? NULL : i->second;
}

TCPSocket *OFA_controller::findSocketFor(uint32_t connId)
{
    SocketMap::iterator i = socketMap.find(connId);
    ASSERT(i==socketMap.end() || i->first==i->second->getConnectionId());
    return (i==socketMap.end()) ? NULL : i->second;
}


void OFA_controller::handleMessage(cMessage *msg)
{
    // self messages used to implement service time
    if (msg->isSelfMessage())
    {
        cMessage *data_msg = (cMessage *) msg->getContextPointer();
        delete msg;
        processQueuedMsg(data_msg);
        if (msg_list.empty())
        {

            busy = false;
        }
        else
        {

            cMessage *msgfromlist = msg_list.front();
            msg_list.pop_front();
            char buf[80];
            sprintf(buf, " %d pakets in queue", msg_list.size());
            getParentModule()->getDisplayString().setTagArg("t", 0, buf);
            std::list<cMessage *>::iterator i = msg_list.begin();
                while (i!=msg_list.end())
                {
                    EV << (*i)->getFullPath() << endl;
                    i++;
                }
            cMessage *event = new cMessage("event");
            event->setContextPointer(msgfromlist);
            scheduleAt(simTime()+serviceTime, event);
        }
    }
    else
    {
        if (busy) {

            msg_list.push_back(msg);

        }
        else
        {

            busy = true;

            cMessage *event = new cMessage("event");
            event->setContextPointer(msg);
            scheduleAt(simTime()+serviceTime, event);
        }
    }

}

void OFA_controller::processQueuedMsg(cMessage *data_msg)
{
    if (data_msg->getKind() == 3)
    {

        TCPSocket *socket = findSocketFor(data_msg);
        if(!socket)
        {
            socket = new TCPSocket(data_msg);
            socket->setOutputGate(gate("tcpOut"));
            ASSERT(socketMap.find(socket->getConnectionId())==socketMap.end());
            socketMap[socket->getConnectionId()] = socket;
            TCPConnectInfo *info = check_and_cast<TCPConnectInfo *>(data_msg->getControlInfo());



            IPv4Address ip_src =  (IPv4Address) (info->getRemoteAddr().toIPv4());
            OF_Wrapper *wrapper = new OF_Wrapper();
            wrapper->connID = socket->getConnectionId();
            wrapper->ip_src = &ip_src;
            emit(connIDSignal, wrapper);
        }

        handshake(data_msg);

    }
    if (dynamic_cast<Open_Flow_Message *>(data_msg) != NULL) {
        Open_Flow_Message *of_msg = (Open_Flow_Message *)data_msg;
        ofp_type type = (ofp_type)of_msg->getHeader().type;
        OF_Wrapper *wrapper = new OF_Wrapper();
        switch (type) {
        case OFPT_PACKET_IN:
            EV << "packet-in message from switch\n";
            wrapper->msg = of_msg;
            EV << getSignalName(PacketInSignalId) << ": " << hasListeners(PacketInSignalId) << " listeners\n";
            emit(PacketInSignalId, wrapper);
            break;
        case OFPT_FEATURES_REPLY:
            handleFeaturesReply(of_msg);
            break;
        case OFPT_FLOW_REMOVED:
            break;

        default:
            break;
        }

    }
    delete data_msg;
}

void OFA_controller::handshake(cMessage *msg)
{

    OFP_Features_Request *featuresRequest = new OFP_Features_Request("FeaturesRequest");
    featuresRequest->getHeader().version = OFP_VERSION;
    featuresRequest->getHeader().type = OFPT_FEATURES_REQUEST;
    featuresRequest->setByteLength(1);
    featuresRequest->setKind(TCP_C_SEND);

    TCPSocket *socket = findSocketFor(msg);
//    delete msg;

    //    SocketMap::iterator i = socketMap.begin();
    //    while (i!=socketMap.end())
    //    {
    //        OFP_Features_Request *copy = featuresRequest->dup();
    //        i->second->send(copy);
    //        i++;
    //    }
    socket->send(featuresRequest);

}

void OFA_controller::handleFeaturesReply(Open_Flow_Message *of_msg)
{
    EV << "OFA_controller::handleFeaturesReply" << endl;
    OFP_Features_Reply *featuresReply = (OFP_Features_Reply *) of_msg;
//    delete of_msg;
}



void OFA_controller::floodPacket(uint32_t buffer_id, OFP_Packet_In *packet_in_msg, int connID)
{
    EV << "OFA_controller::floodPacket" << endl;
    OFP_Packet_Out *packetOut = new OFP_Packet_Out("packetOut");
    packetOut->getHeader().version = OFP_VERSION;
    packetOut->getHeader().type = OFPT_PACKET_OUT;
    packetOut->setBuffer_id(buffer_id);
    packetOut->setByteLength(1);
    if (buffer_id == OFP_NO_BUFFER)
    {
        EthernetIIFrame *frame =  dynamic_cast<EthernetIIFrame *>(packet_in_msg->decapsulate());
        packetOut->encapsulate(frame);

    }


    ofp_action_output *action_output = new ofp_action_output();
    action_output->port = OFPP_FLOOD;
    EV << "outport: " << action_output->port << endl;
    packetOut->setActionsArraySize(1);
    packetOut->setActions(0, *action_output);

    ofp_action_output *output= &packetOut->getActions(0);

    EV << "outport: " << output->port << endl;


    packetOut->setKind(TCP_C_SEND);

    TCPSocket *socket = findSocketFor(packet_in_msg);

    socket->send(packetOut);

}


void OFA_controller::sendPacket(uint32_t buffer_id, OFP_Packet_In *packet_in_msg, uint32_t outport, int connID)
{
    EV << "OFA_controller::sendPacket" << endl;
    OFP_Packet_Out *packetOut = new OFP_Packet_Out("packetOut");
    packetOut->getHeader().version = OFP_VERSION;
    packetOut->getHeader().type = OFPT_PACKET_OUT;
    packetOut->setBuffer_id(buffer_id);
    packetOut->setByteLength(1);
    if (buffer_id == OFP_NO_BUFFER)
    {

        EthernetIIFrame *frame =  dynamic_cast<EthernetIIFrame *>(packet_in_msg->decapsulate());

        packetOut->encapsulate(frame);
    }

    ofp_action_output *action_output = new ofp_action_output();
    action_output->port = outport;
    packetOut->setActionsArraySize(1);
    packetOut->setActions(0, *action_output);


    packetOut->setKind(TCP_C_SEND);

    TCPSocket *socket = findSocketFor(packet_in_msg);

    socket->send(packetOut);

}

void OFA_controller::sendFlowModMessage(ofp_flow_mod_command mod_com, oxm_basic_match *match, uint32_t outport, int connID)
{
    EV << "OFA_controller::sendFlowModMessage" << endl;
    OFP_Flow_Mod *flow_mod_msg = new OFP_Flow_Mod("flow_mod");
    flow_mod_msg->getHeader().version = OFP_VERSION;
    flow_mod_msg->getHeader().type = OFPT_FLOW_MOD;
    flow_mod_msg->setCommand(mod_com);
    flow_mod_msg->setMatch(*match);
    flow_mod_msg->setByteLength(1);
    ofp_action_output *action_output = new ofp_action_output();
    action_output->port = outport;
    flow_mod_msg->setActionsArraySize(1);
    flow_mod_msg->setActions(0, *action_output);

    flow_mod_msg->setKind(TCP_C_SEND);

    TCPSocket *socket = socketMap[connID];
    socket->send(flow_mod_msg);
}

void OFA_controller::processCommand(const cXMLElement &node) {
    //MACAddressTable *target = dynamic_cast<MACAddressTable*>(getModuleByPath(node.getAttribute("target")));
    //cModule *target = getModuleByPath(node.getAttribute("target"));
    Enter_Method("Scenario Manager called");

    if (!strcmp(node.getAttribute("op"), "ofpfc_modify")) {
        ofp_flow_mod_command mod_com = OFPFC_MODIFY;

        oxm_basic_match *match = new oxm_basic_match();
        if(node.getAttribute("src_mac") != nullptr && node.getAttribute("dst_mac") != nullptr) {
            match->OFB_ETH_SRC = MACAddress(node.getAttribute("src_mac"));
            match->OFB_ETH_DST = MACAddress(node.getAttribute("dst_mac"));
            // ipv4
            match->OFB_ETH_TYPE = 0x0800;
            match->wildcards = (OFPFW_IN_PORT);
        }
        uint32_t outport_i = atoi(node.getAttribute("outport"));
        //uint32_t connid_i = atoi(node.getAttribute("connid"));
        const char *peer = node.getAttribute("connid");
        int32_t connid_i = this->findConnIDfor(peer);

        if(connid_i == -1)
            throw omnetpp::cRuntimeError("Can't find Socket for connID.");

        this->sendFlowModMessage(mod_com, match, outport_i, connid_i);

    }
}


int OFA_controller::findConnIDfor(const char *hostname)
{
    L3Address peer = L3AddressResolver().resolve(hostname).toIPv4();

    for(SocketMap::iterator i = socketMap.begin(); i != socketMap.end(); ++i) {

        TCPSocket *s = i->second;
        if(s->getRemoteAddress() == peer) {
            return i->first;
        }
    }
    return -1;
}

