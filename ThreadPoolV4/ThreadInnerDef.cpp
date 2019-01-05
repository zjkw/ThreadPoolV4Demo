#include "stdafx.h"
#include "ThreadInnerDef.h"

task_cls_t GetStdCls(const task_cls_t& cls)
{
	if (cls.empty())
	{
		return task_cls_default;
	}
	else
	{
		return cls;
	}
}