#pragma once

#include <string>
#include "ThreadPoolV4.h"

class CRawTaskData : public ThreadPoolV4::task_data_base
{
public:
	CRawTaskData();
	virtual ~CRawTaskData();

	inline std::string* operator->() { return &_raw_data; }
	inline operator std::string*() { return &_raw_data; }
private:
	std::string	_raw_data;
};

