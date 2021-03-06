// ThreadPoolV4Demo.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "NativeCallWrap_TestCase.h"
#include "NativeCallNative_TestCase.h"
#include "WrapCallWrap_TestCase.h"
#include "WrapCallNative_TestCase.h"

int main()
{
	//测试用例主要考察 非托管线程 和 托管线程的交互；生命期结束处理，这样就有4种基本组合
	//非托管线程意味着不用本线程库创建的线程，反之为托管线程
	//测试内容：单个和多个线程打印那波拉切数字；调用程结束前必须等待工作线程也结束，反之当计算完成，必须通知调用线程再退出
	//测试用例1：托管线程调用非托管线程		->	使用本线程库创建一个线程，调用一个非本库创建的线程
	//测试用例2：托管线程调用托管线程		->	使用本线程库创建一个线程，调用一个本库创建的线程
	//测试用例3：非托管线程调用托管线程		->	使用非本线程库创建一个线程，调用一个本库创建的线程
	//测试用例4：非托管线程调用非托管线程	->	使用非本线程库创建一个线程，调用一个非本库创建的线程

//	CNativeCallWrap_TestCase	case1;
//	CNativeCallNative_TestCase	case1;
	CWrapCallNative_TestCase		case1;
//	CWrapCallWrap_TestCase	case1;

	case1.DoTest();
	PrintMeta();

	system("pause");

	ClearManagedTask();

	PrintMeta();

	system("pause");

    return 0;
}

