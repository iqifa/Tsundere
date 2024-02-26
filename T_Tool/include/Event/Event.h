#pragma once
#ifndef EVENT
#define EVENT

#include<iostream>
#include<functional>
#include<unordered_map>


using namespace std;
using ListenerID = unsigned int;
namespace Eventing {

	template<class... ArgsTypes>
	class Event {
		using CallBack = std::function<void(ArgsTypes...)>;
	public:
		bool RemoveListenerID(ListenerID listenerid);
		ListenerID AddListenerID(CallBack p_call);
	public:
		void Invoke(ArgsTypes... p_args);

		ListenerID operator+=(CallBack p_call);
		bool operator-=(ListenerID listenerid);
	private:
		unordered_map<ListenerID, CallBack>  m_Callbacks;
		int ListenerID_now;
	};
}

#endif // !EVENT