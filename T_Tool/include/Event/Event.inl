#pragma once
#include"Event.h"

namespace Eventing {
	template<class ...ArgsTypes>
	bool Event<ArgsTypes...>::RemoveListenerID(ListenerID listenerid)
	{
		return m_Callbacks.erase(listenerid) != 0;
	}

	template<class ...ArgsTypes>
	ListenerID Event<ArgsTypes...>::AddListenerID(CallBack p_call)
	{
		ListenerID listenerid = ListenerID_now++;
		m_Callbacks[listenerid] = p_call;
		return listenerid;
	}

	template<class... ArgsTypes>
	void Event<ArgsTypes ...>::Invoke(ArgsTypes... p_args)
	{
		for (auto& [key, value] : m_Callbacks)
			value(p_args...);
	}

	template<class ...ArgsTypes>
	ListenerID Event<ArgsTypes...>::operator+=(CallBack p_call)
	{
		return AddListenerID(p_call);
	}

	template<class ...ArgsTypes>
	bool Event<ArgsTypes...>::operator-=(ListenerID listenerid)
	{
		return RemoveListenerID(listenerid);
	}
}
