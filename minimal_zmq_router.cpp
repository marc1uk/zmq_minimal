#include <signal.h>
#include <zmq.hpp>
#include <iostream>
#include<strings.h>    // for strerror
#include <errno.h>     // for errno
#include "errnoname.h" // for errnoname

#include <thread>
#include <chrono>

bool keepgoing;

void sighandler(sig_atomic_t s){
	std::cout<<"Caught stop signal"<<std::endl;
	keepgoing=false;
}

int main(){
	
	zmq::context_t* thecontext = new zmq::context_t(1);
	zmq::socket_t* thesocket = new zmq::socket_t(*thecontext, ZMQ_ROUTER);
	thesocket->setsockopt(ZMQ_LINGER,10);
	thesocket->setsockopt(ZMQ_RCVTIMEO,500);
	thesocket->setsockopt(ZMQ_SNDTIMEO,500);
	thesocket->setsockopt(ZMQ_ROUTER_MANDATORY,1); // router socket should only accept messages
	// to a ZMQ_IDENTITY that is connected already; if set to 0 (default) and no such client
	// is connected the socket will SILENTLY DISCARD THE MESSAGE! applicable to router sockets only.
	//thesocket->bind("tcp://*:2113");           // sane way of being a router
	thesocket->connect("tcp://localhost:2113");  // backwards ServiceDiscovery way
	zmq::pollitem_t inpoll{*thesocket, 0, ZMQ_POLLIN,0};   // ZMQ_THEYSEEMEPOLLIN
	zmq::pollitem_t outpoll{*thesocket, 0, ZMQ_POLLOUT,0};
	
	
	signal(SIGUSR1,sighandler);
	
	bool sendack=false;
	std::string senderID="";
	bool success=true;
	keepgoing=true;
	while(keepgoing){
		int ok=1;
		try {
			ok = zmq::poll(&inpoll,1,500);
		} catch (zmq::error_t& err){
			std::cerr<<"poller caught "<<err.what()<<std::endl;
			ok = -1;
		}
		if(ok<0){
			std::cerr<<"error polling input socket"<<std::endl;
		} else {
			if(inpoll.revents & ZMQ_POLLIN){
				sendack=true;
				std::cout<<"receiving incoming message"<<std::endl;
				// first part for a router is always sender ID
				zmq::message_t tmp;
				ok = thesocket->recv(&tmp);
				if(ok){
					senderID.resize(tmp.size());
					memcpy((void*)senderID.data(),tmp.data(),tmp.size());
					std::cout<<"message from "<<senderID<<std::endl;
				}
				while(tmp.more()){
					// receive any remaining parts
					ok = thesocket->recv(&tmp);
					char msg[tmp.size()];
					memcpy((void*)msg,tmp.data(),tmp.size());
					std::cout<<"next part: "<<msg<<std::endl;
				}
				success = ok;
				if(!success){
					std::cerr<<"failed to receive final part"<<std::endl;
				}
			} else {
				std::cout<<"no incoming message"<<std::endl;
			}
		}
		
		if(sendack){
			try {
				ok = zmq::poll(&outpoll,1,500);
			} catch(zmq::error_t& err){
				std::cerr<<"caught "<<err.what()<<"during outpoll"<<std::endl;
				ok = -1;
			}
			if(ok<0){
				std::cerr<<"error polling output socket!"<<std::endl;
			} else {
				if(outpoll.revents & ZMQ_POLLOUT){
					std::cout<<"sending ack to "<<senderID<<std::endl;
					zmq::message_t idmsg(senderID.length());
					memcpy(idmsg.data(),(void*)senderID.c_str(),senderID.length());
					ok = thesocket->send(idmsg, ZMQ_SNDMORE);
					if(!ok){
						std::cerr<<"error sending ID part"<<std::endl;
					} else {
						int resp=success;
						zmq::message_t tmp(sizeof(resp));
						memcpy(tmp.data(),(void*)&resp,sizeof(resp));
						ok = thesocket->send(tmp);
						if(!ok){
							std::cerr<<"error sending ack"<<std::endl;
							std::cerr<<"errno is "<<errno
							         <<", strerror: '"<<strerror(errno)
							         <<", errnoname: '"<<errnoname(errno)
							         <<"', zmq_strerror: '"<<zmq_strerror(errno)
							         <<"'"<<std::endl;
						} else {
							std::cout<<"sent ack"<<std::endl;
							sendack=false;
						}
					}
				} else {
					std::cerr<<"couldn't send ack; no listener"<<std::endl;
				}
			}
		}
		
	}
	
	std::cout<<"deleting sockets"<<std::endl;
	delete thesocket; thesocket=nullptr;
	
	std::cout<<"deleting context"<<std::endl;
	delete thecontext; thecontext=nullptr;
	
	std::cout<<"done"<<std::endl;
	return 0;
}
