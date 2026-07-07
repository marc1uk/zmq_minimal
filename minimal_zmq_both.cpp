#include <signal.h>
#include <zmq.hpp>
#include <ctime>
#include <iostream>
#include <errno.h>     // for errno
#include "errnoname.h" // for errnoname

#include <thread>
#include <chrono>

bool keepgoing;

void sighandler(sig_atomic_t s){
	std::cout<<"Caught stop signal"<<std::endl;
	keepgoing=false;
}

int main(int argc, const char** argv){
	
	std::string client_identity="bobby";
	if(argc>1) client_identity = std::string{argv[1]};
	std::cout<<"using client identity "<<client_identity<<std::endl;
	client_identity += '\0';
	
	zmq::context_t* thecontext = nullptr;
	zmq::socket_t* thesocket = nullptr;
	bool doitagain=true;
	
	thebeginning:
	thecontext = new zmq::context_t(1);
	thesocket = new zmq::socket_t(*thecontext, ZMQ_DEALER);
	thesocket->setsockopt(ZMQ_LINGER,10);
	thesocket->setsockopt(ZMQ_RCVTIMEO,500);
	thesocket->setsockopt(ZMQ_SNDTIMEO,500);
	//thesocket->setsockopt(ZMQ_IMMEDIATE,1);
	// if set to 1 the socket will BLOCK when you try to send to an endpoint that is not yet
	// connected (i.e. if thesocket->connect has not yet found anyone to connect to)
	// if 0 (default) it will queue the message, but may never have anyone to send it to.
	// alias of ZMQ_DELAY_ATTACH_ON_CONNECT
	thesocket->setsockopt(ZMQ_IDENTITY, client_identity.c_str(),client_identity.length());
	thesocket->bind("tcp://*:1113");                // backwards servicediscovery way.
	zmq::pollitem_t inpoll{*thesocket, 0, ZMQ_POLLIN,0};   // ZMQ_THEYSEEMEPOLLIN
	zmq::pollitem_t outpoll{*thesocket, 0, ZMQ_POLLOUT,0};
	

	zmq::socket_t* thesocket2 = new zmq::socket_t(*thecontext, ZMQ_ROUTER);
	thesocket2->setsockopt(ZMQ_LINGER,10);
	thesocket2->setsockopt(ZMQ_RCVTIMEO,500);
	thesocket2->setsockopt(ZMQ_SNDTIMEO,500);
	thesocket2->setsockopt(ZMQ_ROUTER_MANDATORY,1); // router socket should only accept messages
	// to a ZMQ_IDENTITY that is connected already; if set to 0 (default) and no such client
	// is connected the socket will SILENTLY DISCARD THE MESSAGE! applicable to router sockets only.
	thesocket2->connect("tcp://localhost:1113");  // backwards ServiceDiscovery way
	zmq::pollitem_t inpoll2{*thesocket2, 0, ZMQ_POLLIN,0};   // ZMQ_THEYSEEMEPOLLIN
	zmq::pollitem_t outpoll2{*thesocket2, 0, ZMQ_POLLOUT,0};

	
	signal(SIGUSR1,sighandler);
	
	time_t last_send;
	time(&last_send);
	double seconds_between_sends = 1;

	bool sendack=false;
	std::string senderID="";
	bool success=true;
	int msgnum=0;
	int msgnum2=99;
	
	keepgoing=true;
	std::cout<<"waiting for connection."<<std::endl;
	while(keepgoing){
		int ok=-1;
		try{
			ok = zmq::poll(&outpoll, 1, 100);  // poll for 100ms for a connection
		} catch(zmq::error_t& err){
			// ignore poll aborting due to signals
			if(zmq_errno()==EINTR) continue;
			throw;
		}
		//std::cout<<"zmq::poll returned: "<<ok<<std::endl;
		if(ok<0){
			std::cerr<<"Polling error "<<zmq_strerror(errno)<<std::endl;
		} else if(ok==0){
			// no connection, technically this sets errno such that zmq_strerror gives
			// 'resource temporarily unavailable'
			std::cout<<"."<<std::flush;
		} else if(outpoll.revents & ZMQ_POLLOUT){
			std::cout<<"Connected!"<<std::endl;
			break;
		}
	}
	
	while(keepgoing){
		
		time_t now;
		time(&now);
		double secs_since_last_send = difftime(now, last_send);
		std::cout<<"secs since last send: " <<secs_since_last_send
		         <<", compared to ticks between sends: "<<seconds_between_sends<<std::endl;
		
		if(secs_since_last_send>seconds_between_sends){
			int ok=0;
			try {
				ok = zmq::poll(&outpoll,1,500);
			} catch(zmq::error_t& err){
				if(zmq_errno()==EINTR) continue;
				std::cerr<<"poll caught "<<err.what()<<std::endl;
				ok = -1;
			}
			if(ok<0){
				std::cerr<<"error polling output socket"<<std::endl;
			} else {
				if(outpoll.revents & ZMQ_POLLOUT){
					// TODO add a message id
					std::cout<<"sending next_message: "<<msgnum<<std::endl;
					std::string msg = std::to_string(msgnum++);
					if(msgnum>20) msgnum=0;
					zmq::message_t tmp(msg.length());
					memcpy(tmp.data(),(void*)msg.c_str(),msg.length());
					ok = thesocket->send(tmp);
					if(!ok){
						std::cerr<<"error sending message"<<std::endl;
					} else {
						std::cout<<"sent message"<<std::endl;
					}
					time(&last_send);
				} else {
					std::cerr<<"couldn't send message; no listener"<<std::endl;
				}
			}
		}
		
		int ok=0;
		try {
			ok = zmq::poll(&inpoll,1,500);
		} catch(zmq::error_t& err){
			if(zmq_errno()==EINTR) continue;
			std::cerr<<"poll caught "<<err.what()<<std::endl;
			ok=-1;
		}
		if(ok<0){
			std::cerr<<"error polling input socket"<<std::endl;
		} else {
			if(inpoll.revents & ZMQ_POLLIN){
				std::cout<<"receiving ack"<<std::endl;
				zmq::message_t tmp;
				int ok = thesocket->recv(&tmp);
				int ack=0;
				memcpy(&ack,tmp.data(),tmp.size());
				std::cout<<"ack: "<<ack<<std::endl;
				// TODO check ack id matches sent message id
				while(ok && tmp.more()){
					std::cout<<"unexpected extra part in ack!"<<std::endl;
					ok = thesocket->recv(&tmp);
				}
				if(!ok){
					std::cerr<<"failed to receive final part"<<std::endl;
				}
			} // else no incoming message
		}
		
		try {
			ok = zmq::poll(&inpoll2,1,500);
		} catch (zmq::error_t& err){
			std::cerr<<"poller caught "<<err.what()<<std::endl;
			ok = -1;
		}
		if(ok<0){
			std::cerr<<"error polling input socket"<<std::endl;
		} else {
			if(inpoll2.revents & ZMQ_POLLIN){
				sendack=true;
				std::cout<<"receiving incoming message2"<<std::endl;
				// first part for a router is always sender ID
				zmq::message_t tmp;
				ok = thesocket2->recv(&tmp);
				if(ok){
					senderID.resize(tmp.size());
					memcpy((void*)senderID.data(),tmp.data(),tmp.size());
					std::cout<<"message from "<<senderID<<std::endl;
				} else {
					std::cout<<"error receiving sender ID"<<std::endl;
				}
				while(tmp.more()){
					// receive any remaining parts
					ok = thesocket2->recv(&tmp);
					char msg[tmp.size()];
					memcpy((void*)msg,tmp.data(),tmp.size());
					msg[tmp.size()]='\0';
					std::cout<<"next part: "<<msg<<std::endl;
					msgnum2=std::atoi(msg);
				}
				success = ok;
				if(!success){
					std::cerr<<"failed to receive final part"<<std::endl;
				}
			} else {
				std::cout<<"no incoming message on socket2"<<std::endl;
			}
		}
		
		if(sendack){
			try {
				ok = zmq::poll(&outpoll2,1,500);
			} catch(zmq::error_t& err){
				std::cerr<<"caught "<<err.what()<<"during outpoll"<<std::endl;
				ok = -1;
			}
			if(ok<0){
				std::cerr<<"error polling output socket!"<<std::endl;
			} else {
				if(outpoll2.revents & ZMQ_POLLOUT){
					std::cout<<"sending ack to "<<senderID<<std::endl;
					zmq::message_t idmsg(senderID.length());
					memcpy(idmsg.data(),(void*)senderID.c_str(),senderID.length());
					ok = thesocket2->send(idmsg, ZMQ_SNDMORE);
					if(!ok){
						std::cerr<<"error sending ID part"<<std::endl;
					} else {
						int resp=-msgnum2;
						zmq::message_t tmp(sizeof(resp));
						memcpy(tmp.data(),(void*)&resp,sizeof(resp));
						ok = thesocket2->send(tmp);
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
	
	/*
	if(doitagain){
		std::cout<<"doing it all over again"<<std::endl;
		doitagain=false;
		goto thebeginning;
	}
	*/
	
	return 0;
}
