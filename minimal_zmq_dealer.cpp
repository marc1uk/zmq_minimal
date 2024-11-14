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
	//thesocket->connect("tcp://localhost:2113");   // sane way of being a dealer
	thesocket->bind("tcp://*:2113");                // backwards servicediscovery way.
	zmq::pollitem_t inpoll{*thesocket, 0, ZMQ_POLLIN,0};   // ZMQ_THEYSEEMEPOLLIN
	zmq::pollitem_t outpoll{*thesocket, 0, ZMQ_POLLOUT,0};
	
	
	signal(SIGUSR1,sighandler);
	
	time_t last_send;
	time(&last_send);
	double seconds_between_sends = 1;
	
	keepgoing=true;
	std::cout<<"waiting for connection."<<std::endl;
	while(keepgoing){
		int ok = zmq::poll(&outpoll,1,100); // poll for 100ms for a connection
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
				std::cerr<<"poll caught "<<err.what()<<std::endl;
				ok = -1;
			}
			if(ok<0){
				std::cerr<<"error polling output socket"<<std::endl;
			} else {
				if(outpoll.revents & ZMQ_POLLOUT){
					std::cout<<"sending next_message"<<std::endl;
					// TODO add a message id
					std::string msg = "Echo!";
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
				int ack;
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
