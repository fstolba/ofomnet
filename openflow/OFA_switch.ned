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

package openflow;

////The Switch Application module is a TCP application and therefore directly connected
//to the TCP module. It belongs to the control plane and is responsible for
//the communication with the controller.
simple OFA_switch
{
    parameters:
        @display("i=block/app");
        @signal[NF_FLOOD_PACKET];
        @signal[NF_SEND_PACKET];
        @signal[NF_NO_MATCH_FOUND];
        string address = default("");
        int port = default(-1);
        string connectAddress = default("");
        int connectPort = default(1000);
        double startTime @unit("s") = default(0s);
        bool sendCompletePacket = default(false);
		double flow_timeout @unit("s");
		
    gates:
        input tcpIn @labels(TCPCommand/up);
        output tcpOut @labels(TCPCommand/down);

}
