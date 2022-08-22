// CaptureDemo.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"


#include <dshow.h>
#include "qedit.h"
#include <iostream>
#include <objbase.h>
#include <atlconv.h>
#include <strmif.h>
#include <vidcap.h>         // For IKsTopologyInfo  
#include <ksproxy.h>        // For IKsControl  
#include <ks.h>
#include <ksmedia.h>
#include <Windows.h>

#pragma comment(lib,"strmiids.lib")
#pragma comment(lib,"strmbase.lib")

#define DEFAULT_VIDEO_WIDTH     1280
#define DEFAULT_VIDEO_HEIGHT    1024

#define JM_TOF_DEPTHDATA_LENGTH		240*180*10

// this object is a SEMI-COM object, and can only be created statically.

enum PLAYSTATE { Stopped, Paused, Running, Init };
PLAYSTATE psCurrent = Stopped;

IMediaControl *pMediaControl = NULL;
IMediaEvent *pMediaEvent = NULL;
IGraphBuilder *pGraphBuilder = NULL;
ICaptureGraphBuilder2 *pCaptureGraphBuilder2 = NULL;
IVideoWindow *pVideoWindow = NULL;
IMoniker *pMonikerVideo = NULL;
IBaseFilter *pVideoCaptureFilter = NULL;
IBaseFilter *pGrabberF = NULL;
ISampleGrabber *pSampleGrabber = NULL;

IBaseFilter *pGrabberStill = NULL;
ISampleGrabber *pSampleGrabberStill = NULL;
IBaseFilter *pNull = NULL;

//ץ�Ļص�
class CSampleGrabberCB : public ISampleGrabberCB 
{
public:

	long Width;
	long Height;

	HANDLE BufferEvent;
	LONGLONG prev, step;
	DWORD lastTime;

	bool OneShot = true;

	// Fake out any COM ref counting
	STDMETHODIMP_(ULONG) AddRef() { return 2; }
	STDMETHODIMP_(ULONG) Release() { return 1; }

	CSampleGrabberCB()
	{
		lastTime =0;
	}
	// Fake out any COM QI'ing
	STDMETHODIMP QueryInterface(REFIID riid, void ** ppv)
	{
		//CheckPointer(ppv,E_POINTER);

		if( riid == IID_ISampleGrabberCB || riid == IID_IUnknown ) 
		{
			*ppv = (void *) static_cast<ISampleGrabberCB*> ( this );
			return NOERROR;
		}    

		return E_NOINTERFACE;
	}

	STDMETHODIMP SampleCB( double SampleTime, IMediaSample * pSample )
	{
		return 0;
	}

	STDMETHODIMP BufferCB( double SampleTime, BYTE * pBuffer, long BufferSize )
	{
#if 0
		int i;
		for (i = 0; i < 4; i++)
			printf("%02x ", pBuffer[i]);
		printf("\t");
		for (i = 0; i < 4; i++)
			printf("%02x ", pBuffer[JM_TOF_DEPTHDATA_LENGTH+i]);
		printf("\t");
		printf("size=%d\n", BufferSize);
		return 0;
#else
		//���ݸ�ʽ: TOF + MJPG
		char FileName[256];

		if (OneShot)
		{
			FILE* out;
			int mjpgSize = BufferSize - JM_TOF_DEPTHDATA_LENGTH;

			sprintf_s(FileName, "capture_%d.jpg", (int)GetTickCount());
			out = fopen(FileName, "wb");
			fwrite(pBuffer, 1, mjpgSize, out);
			fclose(out);

			sprintf_s(FileName, "capture_tof_%d.rgb", (int)GetTickCount());
			out = fopen(FileName, "wb");
			fwrite(pBuffer+ mjpgSize, 1, JM_TOF_DEPTHDATA_LENGTH, out);
			fclose(out);

			int i;
			for (i = 0; i < 4; i++)
				printf("%02x ", pBuffer[i]);
			printf("\t");
			for (i = 0; i < 4; i++)
				printf("%02x ", pBuffer[mjpgSize-(4-i)]);
			printf("\t");
			printf("size=%d\n", BufferSize);

			OneShot = false;
		}
		return 0;
#endif
	}
};

//����Ԥ������λ��
void SetupVideoWindow(void)
{
	pVideoWindow->put_Left(0); 
	pVideoWindow->put_Width(DEFAULT_VIDEO_WIDTH); 
	pVideoWindow->put_Top(0); 
	pVideoWindow->put_Height(DEFAULT_VIDEO_HEIGHT); 
	pVideoWindow->put_Caption(L"Video Window");
}

HRESULT GetInterfaces(void)
{
	HRESULT hr;

	//����Filter Graph Manager.
	hr = CoCreateInstance (CLSID_FilterGraph, NULL, CLSCTX_INPROC,
		IID_IGraphBuilder, (void **) &pGraphBuilder);
	if (FAILED(hr))
		return hr;
	//����Capture Graph Builder.
	hr = CoCreateInstance (CLSID_CaptureGraphBuilder2 , NULL, CLSCTX_INPROC,
		IID_ICaptureGraphBuilder2, (void **) &pCaptureGraphBuilder2);
	if (FAILED(hr))
		return hr;

	// IMediaControl�ӿڣ�����������ý����Filter Graph�е�������������ý���������ֹͣ��
	hr = pGraphBuilder->QueryInterface(IID_IMediaControl,(LPVOID *) &pMediaControl);
	if (FAILED(hr))
		return hr;

	// IVideoWindow,������ʾԤ����Ƶ
	hr = pGraphBuilder->QueryInterface(IID_IVideoWindow, (LPVOID *) &pVideoWindow);
	if (FAILED(hr))
		return hr;

	// IMediaEvent�ӿڣ��ýӿ���Filter Graph����һЩ�¼�ʱ���������¼��ı�־��Ϣ�����͸�Ӧ�ó���
	hr = pGraphBuilder->QueryInterface(IID_IMediaEvent,(LPVOID *) &pMediaEvent);
	if (FAILED(hr))
		return hr;

	//��������Ԥ����Sample Grabber Filter.
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&pGrabberF);
	if (FAILED(hr))
		return hr;

	//��ȡISampleGrabber�ӿڣ��������ûص��������Ϣ
	hr = pGrabberF->QueryInterface(IID_ISampleGrabber, (void**)&pSampleGrabber);
	if (FAILED(hr)) 
		return hr;

	//��������ץ�ĵ�Sample Grabber Filter.
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,IID_IBaseFilter, (void**)&pGrabberStill);
	if (FAILED(hr))
		return hr;

	//��ȡISampleGrabber�ӿڣ��������ûص��������Ϣ
	hr = pGrabberStill->QueryInterface(IID_ISampleGrabber, (void**)&pSampleGrabberStill);
	if (FAILED(hr)) 
		return hr;

	//����Null Filter
	hr = CoCreateInstance(CLSID_NullRenderer,NULL,CLSCTX_INPROC_SERVER,IID_IBaseFilter,(void**)&pNull);
	if (FAILED(hr))
		return hr;
	return hr;
}

//�رսӿ�
void CloseInterfaces(void)
{
	if (pMediaControl)
		pMediaControl->StopWhenReady();
	psCurrent = Stopped;

	if(pVideoWindow) 
		pVideoWindow->put_Visible(OAFALSE);

	pMediaControl->Release();
	pGraphBuilder->Release();
	pVideoWindow->Release();
	pCaptureGraphBuilder2->Release();
}

//�����豸���ҵ�ָ��vidpid�豸����豸
HRESULT InitMonikers()
{
	USES_CONVERSION;
	HRESULT hr;
	ULONG cFetched;

	//https://docs.microsoft.com/zh-cn/windows/desktop/DirectShow/selecting-a-capture-device
	//�����豸
	ICreateDevEnum *pCreateDevEnum;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pCreateDevEnum);
	if (FAILED(hr))
	{
		printf("Failed to enumerate all video and audio capture devices!  hr=0x%x\n", hr);
		return hr;
	}

	IEnumMoniker *pEnumMoniker;
	hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumMoniker, 0);
	if (FAILED(hr) || !pEnumMoniker)
	{
		printf("Failed to create ClassEnumerator!  hr=0x%x\n", hr);
		return -1;
	}

	while (hr = pEnumMoniker->Next(1, &pMonikerVideo, &cFetched), hr == S_OK)
	{
		IPropertyBag *pPropBag;
		//BindToStorage֮��Ϳ��Է����豸��ʶ�����Լ��ˡ�
		HRESULT hr = pMonikerVideo->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
		if (FAILED(hr))
		{
			pMonikerVideo->Release();
			continue;
		}

		VARIANT var;
		VariantInit(&var);
		//DevicePath�а���vidpid
		hr = pPropBag->Read(L"DevicePath", &var, 0);
		if (FAILED(hr))
		{
			VariantClear(&var);
			pMonikerVideo->Release();
			continue;
		}

		//�Ƚ��Ƿ���Ҫʹ�õ��豸
		//TRACE("Device path: %S\n", var.bstrVal);
		std::string devpath = std::string(W2A(var.bstrVal));
		if (devpath.find("vid_1d6b&pid_0102") == -1)
		{
			VariantClear(&var);
			pMonikerVideo->Release();
			continue;
		}
		VariantClear(&var);

		// BindToObject��ĳ���豸��ʶ�󶨵�һ��DirectShow Filter��
		//     Ȼ�����IFilterGraph::AddFilter���뵽Filter Graph�У�����豸�Ϳ��Բ��빤����
		// ����IMoniker::BindToObject����һ����ѡ���device���ϵ�filter��
		//      ����װ��filter������(CLSID,FriendlyName, and DevicePath)��
		hr = pMonikerVideo->BindToObject(0, 0, IID_IBaseFilter, (void**)&pVideoCaptureFilter);
		if (FAILED(hr))
		{
			printf("Couldn't bind moniker to filter object!  hr=0x%x\n", hr);
			return hr;
		}

		pPropBag->Release();
		pMonikerVideo->Release();
	}

	pEnumMoniker->Release();
	return hr;
}

//https://docs.microsoft.com/zh-cn/windows/desktop/DirectShow/capturing-an-image-from-a-still-image-pin
//���嵽���豸��
//USB Camera������Pin
//Capture pin��Still pin
//Capture pin������Ƶ��Ԥ��
//Still pin������Ӧץ�ģ�����������Ӳ��������
//��Ҫʹ��Still pin��������������Capture pin����������ʹ��Still pin
HRESULT CaptureVideo()
{
	//DirectShow�Ľӿ�ʹ����COM����������Ҫ��ʼ��com����
	HRESULT hr = CoInitialize(NULL);

	//https://docs.microsoft.com/zh-cn/windows/desktop/DirectShow/about-the-capture-graph-builder
	//��ʼ���ӿ�
	hr = GetInterfaces();
	if (FAILED(hr))
	{
		printf("Failed to get video interfaces!  hr=0x%x\n", hr);
		return hr;
	}

	// ��ʼ�� Capture Graph Builder.
	hr = pCaptureGraphBuilder2->SetFiltergraph(pGraphBuilder);
	if (FAILED(hr))
	{
		printf("Failed to attach the filter graph to the capture graph!  hr=0x%x\n", hr);
		return hr;
	}

	//����豸
	hr = InitMonikers();
	if(FAILED(hr))
	{
		printf("Failed to InitMonikers!  hr=0x%x\n", hr);
		return hr;
	}

	//��ʼ����Ԥ��graph
	//�����豸ץ��filter
	hr = pGraphBuilder->AddFilter(pVideoCaptureFilter, L"Video Capture");
	if (FAILED(hr))
	{
		printf("Couldn't add video capture filter to graph!  hr=0x%x\n", hr);
		pVideoCaptureFilter->Release();
		return hr;
	}

	//����DirectShow���Դ���SampleGrabber Filter
	hr = pGraphBuilder->AddFilter(pGrabberF, L"Sample Grabber");
	if (FAILED(hr))
	{
		printf("Couldn't add sample grabber to graph!  hr=0x%x\n", hr);
		// Return an error.
	}

	//ʹ��Capture Graph Builder����Ԥ�� Graph
	hr = pCaptureGraphBuilder2->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pVideoCaptureFilter, pGrabberF, 0 );
	if (FAILED(hr))
	{
		printf("Couldn't render video capture stream. The device may already be in use.  hr=0x%x\n", hr);
		pVideoCaptureFilter->Release();
		return hr;
	}

	//����DirectShow���Դ���SampleGrabber Filter
	hr = pGraphBuilder->AddFilter(pGrabberStill, L"Still Sample Grabber");
	if (FAILED(hr))
	{
		printf("Couldn't add sample grabber to graph!  hr=0x%x\n", hr);
		// Return an error.
	}

	//����DirectShow���Դ���NullRender Filter
	hr = pGraphBuilder->AddFilter(pNull, L"NullRender");
	if (FAILED(hr))
	{
		printf("Couldn't add null to graph!  hr=0x%x\n", hr);
		return hr;
	}

	//ʹ��Capture Graph Builder����ץ�� Graph
	hr = pCaptureGraphBuilder2->RenderStream(&PIN_CATEGORY_STILL, &MEDIATYPE_Video, pVideoCaptureFilter, pGrabberStill, pNull);
	if (FAILED(hr))
	{
		printf("Couldn't render video capture stream. The device may already be in use.  hr=0x%x\n", hr);
		pVideoCaptureFilter->Release();
		return hr;
	}

	//configure the Sample Grabber so that it buffers samples :
	hr = pSampleGrabberStill->SetOneShot(FALSE);
	hr = pSampleGrabberStill->SetBufferSamples(TRUE);

	//��ȡ�豸�����ʽ��Ϣ
	AM_MEDIA_TYPE mt;
	hr = pSampleGrabber->GetConnectedMediaType(&mt);
	if (FAILED(hr))
	{
		return -1;
	}
	VIDEOINFOHEADER * vih = (VIDEOINFOHEADER*)mt.pbFormat;
	CSampleGrabberCB *CB = new CSampleGrabberCB();
	if (!FAILED(hr))
	{
		CB->Width = vih->bmiHeader.biWidth;
		CB->Height = vih->bmiHeader.biHeight;
	}

	//����Ԥ����ʼ����õĻص����� 0-SampleCB 1-BufferCB
	hr = pSampleGrabber->SetCallback(CB, 1);
	if (FAILED(hr))
	{
		printf("set preview video call back failed\n");
	}

/*
	//���ô���ץ�ĺ󣬵��õĻص����� 0-����SampleCB 1-BufferCB
	hr = pSampleGrabberStill->SetCallback(CB, 1);
	if (FAILED(hr))
	{
		printf("set still trigger call back failed\n");
	}
*/
	//pVideoCaptureFilter->Release();

	//����Ԥ�����ڴ�Сλ��
	SetupVideoWindow();

	//�����豸
	hr = pMediaControl->Run();
	if (FAILED(hr))
	{
		printf("Couldn't run the graph!  hr=0x%x\n", hr);
		return hr;
	}
	else 
		psCurrent = Running;

#if 0
	IKsTopologyInfo *pInfo = NULL;
	hr = pVideoCaptureFilter->QueryInterface(__uuidof(IKsTopologyInfo),(void **)&pInfo);
	if (SUCCEEDED(hr))
	{
		DWORD dwNumNodes = 0;
		DWORD dwINode = 3;
		pInfo->get_NumNodes(&dwNumNodes);
		for (int i = 0;i < dwNumNodes;i++)
		{
			GUID nodeType;
			pInfo->get_NodeType(i,&nodeType);
			dwINode = i;	
		}
			
	}

	IKsControl *pCtl = NULL;
	hr = pInfo->CreateNodeInstance(0,IID_IKsControl,(void **)&pCtl);
	//hr = pVideoCaptureFilter->QueryInterface(IID_IKsControl, (void **)&pCtl);
	if(FAILED(hr)) return (0);

	HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hEvent)
	{
		printf("CreateEvent failed\n");
		return -1;
	}
	
	KSEVENT Event;
	Event.Set = KSEVENTSETID_VIDCAPNotify;
	Event.Id = KSEVENT_VIDCAPTOSTI_EXT_TRIGGER;
	Event.Flags = KSEVENT_TYPE_ENABLE ;


	KSEVENTDATA EventData;

	EventData.NotificationType = KSEVENTF_EVENT_HANDLE;
	EventData.EventHandle.Event = hEvent;
	EventData.EventHandle.Reserved[0] = 0;
	EventData.EventHandle.Reserved[1] = 0;

	ULONG ulBytesReturned = 0L;
	// register for autoupdate events
	hr = pCtl->KsEvent(
		&Event, 
		sizeof(Event), 
		&EventData, 
		sizeof(KSEVENTDATA), 
		&ulBytesReturned);
	if (FAILED(hr))
	{
		printf("Failed to register for auto-update event : %x\n", hr);
		return -1;
	}

	// Wait for event for 5 seconds 
	DWORD dwError = WaitForSingleObject(hEvent, 50000);

	// cancel further notifications
	hr = pCtl->KsEvent(
		NULL, 
		0, 
		&EventData, 
		sizeof(KSEVENTDATA), 
		&ulBytesReturned);
	if (FAILED(hr))  printf("Cancel event returns : %x\n", hr);

	if ((dwError == WAIT_FAILED) || 
		(dwError == WAIT_ABANDONED) ||
		(dwError == WAIT_TIMEOUT))
	{
		printf("Wait failed : %d\n", dwError);
		return -1;
	} 
	printf("Wait returned : %d\n", dwError);
#endif

	return hr;
}

//ֹͣԤ��
void StopPreview()
{
	pMediaControl->Stop();
	CloseInterfaces();
	CoUninitialize();
	psCurrent = Stopped;
}

int main()
{
	HRESULT hr;														
	char cmd;
	printf("p - Play Video\ns - Stop Video\nq - Quit\n\n");

	while (true)
	{
		std::cin >> cmd;
		switch(cmd)
		{
		case 'p': 
			{															
				printf("	Play Video!\n");
				hr = CaptureVideo();
				if (FAILED(hr))	
					printf("Error!");
			}
			break;
		case 's': 
			{															
				printf("	Stop Video!\n");
				if (psCurrent == Running) StopPreview();
				else printf ("Video already stopped.\n");
			}
			break;
		case 'q': 
			return 0;											
			break;
		default: printf("Unknown command!\n");
			break;
		}
	}
}