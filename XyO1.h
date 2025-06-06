#pragma once

#include "resource.h"
#include<d2d1.h>
#include<WS2tcpip.h>
#include<WinSock2.h>
#include<mfcaptureengine.h>
#include<mfapi.h>
#include<d3d11.h>
#include<dxgi1_2.h>
#include<chrono>
#include<thread>
#include<mftransform.h>
#include<wmcodecdsp.h>
#include<codecapi.h>
#include<Mferror.h>

#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"ws2_32.lib")

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"wmcodecdspuuid.lib")
#pragma comment(lib,"mf.lib")

ID2D1Factory* factory = NULL;
ID2D1HwndRenderTarget* prt = NULL;

int width = 0;
int height = 0;

SOCKET s;

ID3D11Device* device = NULL;
ID3D11DeviceContext* ctx = NULL;

ID3D11VideoDevice* videoDevice = NULL;
D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
ID3D11VideoContext* videoContext = NULL;
ID3D11VideoProcessorEnumerator* vpe = NULL;
D3D11_VIDEO_PROCESSOR_CAPS caps;
ID3D11VideoProcessor* processor = NULL;

UINT64 duration;


IMFTransform* transform = NULL;

ID3D11Texture2D* to_format(ID3D11Texture2D* texture, DXGI_FORMAT format);

void init(HWND h) {

	RECT r;
	GetClientRect(h, &r);

	width = r.right;
	height = r.bottom;

	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
	if (FAILED(hr)) {
		return;
	}
	D2D1_RENDER_TARGET_PROPERTIES properties{};
	properties.dpiX = 96.0f;
	properties.dpiY = 96.0f;
	properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
	properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
	properties.type = D2D1_RENDER_TARGET_TYPE_HARDWARE;
	properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;

	hr = factory->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(h, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE), &prt);
	if (FAILED(hr)) {
		return;
	}

	WSAData wsaData = { 0 };
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		return;
	}
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		WSACleanup();

		return;
	}

	sockaddr_in sock;

	//inet_pton(AF_INET, "0.0.0.0", &sock.sin_addr);
	sock.sin_family = AF_INET;
	sock.sin_port = htons(5000);
	sock.sin_addr.S_un.S_addr = INADDR_ANY;
	sock.sin_port = htons(5000);
	bind(s, (SOCKADDR*)&sock, sizeof(sock));
	listen(s, 5);

	s = accept(s, NULL, NULL);
	if (s == INVALID_SOCKET) {
		WSACleanup();
		return;
	}



	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL featureLevel;

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(hr)) {
		return;
	}

	hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &device, &featureLevel, &ctx);
	if (FAILED(hr)) {
		return;
	}

	IDXGIDevice1* dxgiDevice = NULL;
	hr = device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice);
	if (FAILED(hr)) {
		return;
	}
	IDXGIAdapter* adapter = NULL;
	hr = dxgiDevice->GetAdapter(&adapter);
	if (FAILED(hr)) {
		return;
	}
	dxgiDevice->Release();
	dxgiDevice = NULL;
	IDXGIOutput* output = NULL;
	hr = adapter->EnumOutputs(0, &output);
	if (FAILED(hr)) {
		return;
	}
	adapter->Release();
	adapter = NULL;

	DXGI_OUTPUT_DESC desc;
	output->GetDesc(&desc);

	IDXGIOutput1* output1 = NULL;
	hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
	if (FAILED(hr)) {
		return;
	}

	IDXGIOutputDuplication* dxgiOutputDuplication = NULL;
	hr = output1->DuplicateOutput(device, &dxgiOutputDuplication);
	if (FAILED(hr)) {
		return;
	}

	output->Release();
	output1->Release();
	output = NULL;
	output1 = NULL;

	DXGI_OUTDUPL_DESC dxgi_outdupl_desc{ 0 };
	dxgiOutputDuplication->GetDesc(&dxgi_outdupl_desc);
	width = dxgi_outdupl_desc.ModeDesc.Width;
	height = dxgi_outdupl_desc.ModeDesc.Height;

	DXGI_OUTDUPL_FRAME_INFO frame_info{ 0 };

	IDXGIResource* resource = NULL;

	hr = MFStartup(MF_VERSION, 0);
	if (FAILED(hr)) {
		return;
	}

	IMFPresentationClock* presentation = NULL;
	IMFPresentationTimeSource* m_pTimeSrc = NULL;
	hr = MFCreatePresentationClock(&presentation);
	if (FAILED(hr)) {
		return;
	}
	hr = MFCreateSystemTimeSource(&m_pTimeSrc);
	if (FAILED(hr)) {
		return;
	}
	hr = presentation->SetTimeSource(m_pTimeSrc);
	if (FAILED(hr)) {
		return;
	}
	hr = presentation->Start(0);
	LONGLONG sampleTime = 0;

	LONGLONG last_time = 0;

	hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_IMFTransform, reinterpret_cast<void**>(&transform));
	if (FAILED(hr)) {
		return;
	}
	IMFMediaType* mediaType = NULL;
	hr = MFCreateMediaType(&mediaType);
	if (FAILED(hr)) {
		return;
	}
	hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (FAILED(hr)) {
		return;
	}
	hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (FAILED(hr)) {
		return;
	}
	hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, 30u, 1u);
	if (FAILED(hr)) {
		return;
	}
	hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
	if (FAILED(hr)) {
		return;
	}
	hr = mediaType->SetUINT32(MF_MT_AVG_BITRATE, width * height * 30);
	if (FAILED(hr)) {
		return;
	}
	hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (FAILED(hr)) {
		return;
	}
	hr = transform->SetOutputType(0, mediaType, 0);
	if (FAILED(hr)) {
		return;
	}

	IMFMediaType* inputMediaType = NULL;
	for (DWORD index = 0;index < 15;index++) {
		hr = transform->GetInputAvailableType(0, index, &inputMediaType);
		if (FAILED(hr)) {
			return;
		}
		GUID subtype;
		hr = inputMediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (subtype == MFVideoFormat_NV12) {
			hr = transform->SetInputType(0, inputMediaType, 0);
			break;
		}
	}
	mediaType->Release();
	mediaType = NULL;
	inputMediaType->Release();
	inputMediaType = NULL;

	MFT_OUTPUT_STREAM_INFO info{};
	hr = transform->GetOutputStreamInfo(0, &info);

	MFFrameRateToAverageTimePerFrame(30u, 1u, &duration);

	transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
	transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

	hr = device->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&videoDevice);
	if (FAILED(hr)) {
		return;
	}
	contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	contentDesc.InputWidth = width;
	contentDesc.InputHeight = height;
	contentDesc.OutputHeight = height;
	contentDesc.OutputWidth = width;
	contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

	hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &vpe);
	if (FAILED(hr)) {
		return;
	}
	hr = vpe->GetVideoProcessorCaps(&caps);
	if (FAILED(hr)) {
		return;
	}
	hr = videoDevice->CreateVideoProcessor(vpe, 0, &processor);
	if (FAILED(hr)) {
		return;
	}

	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT format1 = DXGI_FORMAT_NV12;
	RECT src{ 0,0,static_cast<LONG>(contentDesc.InputWidth),static_cast<LONG>(contentDesc.InputHeight) };
	RECT dest{ 0,0,static_cast<LONG>(contentDesc.OutputWidth),static_cast<LONG>(contentDesc.OutputHeight) };

	DXGI_MODE_DESC1 modeFilter{};
	modeFilter.Width = width;
	modeFilter.Height = height;
	hr = ctx->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&videoContext);
	if (FAILED(hr)) {
		return;
	}

	videoContext->VideoProcessorSetStreamFrameFormat(processor, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
	videoContext->VideoProcessorSetStreamOutputRate(processor, 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, TRUE, NULL);

	RECT r1 = src;
	videoContext->VideoProcessorSetStreamSourceRect(processor, 0, TRUE, &r1);
	videoContext->VideoProcessorSetStreamDestRect(processor, 0, TRUE, &r1);
	videoContext->VideoProcessorSetOutputTargetRect(processor, TRUE, &r1);

	D3D11_VIDEO_COLOR background{};
	background.RGBA.A = 0.3f;
	background.RGBA.B = 1.0f;
	background.RGBA.G = 1.0f;
	background.RGBA.R = 1.0f;
	videoContext->VideoProcessorSetOutputBackgroundColor(processor, FALSE, &background);

	IMFDXGIDeviceManager* dxgiDeviceManager = NULL;
	UINT32 token;
	hr = MFCreateDXGIDeviceManager(&token, &dxgiDeviceManager);
	if (SUCCEEDED(hr)) {

	}


	std::thread thread([&] {
		UINT32 sampleSize = 0;
		while (true) {
			IMFSample* sample = NULL;
			MFCreateSample(&sample);
			IMFMediaBuffer* buffer = NULL;
			MFCreateMemoryBuffer(info.cbSize, &buffer);
			sample->AddBuffer(buffer);

			MFT_OUTPUT_DATA_BUFFER outputDataBuffer{ 0,sample };
			DWORD status = 0;
			hr = transform->ProcessOutput(0, 1, &outputDataBuffer, &status);
			if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
				IMFMediaType* mediaType = NULL;
				//hr = transform->GetOutputAvailableType(0, 0, &mediaType);
				//if (SUCCEEDED(hr)) {
				//	hr = transform->SetOutputType(0, mediaType, 0);
				//	if (SUCCEEDED(hr)) {
				//		hr = transform->GetOutputStreamInfo(0, &info);
				//	}
				//}
				for (DWORD index = 0;index < 15;index++) {
					hr = transform->GetOutputAvailableType(0, index, &mediaType);
					if (SUCCEEDED(hr)) {
						GUID subtype;
						hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);

						if (subtype == MFVideoFormat_H264) {
							mediaType->SetUINT32(MF_MT_AVG_BITRATE, width * height * 30);
							mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
							MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
							MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
							MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, 30u, 1u);

							hr = transform->SetOutputType(0, mediaType, 0);
							if (SUCCEEDED(hr)) {
								hr = transform->GetOutputStreamInfo(0, &info);

								mediaType->GetUINT32(MF_MT_SAMPLE_SIZE, &sampleSize);
							}

							else if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {

							}
						}
					}
				}
			}
			if (SUCCEEDED(hr)) {
				buffer->Release();
				buffer = NULL;
				hr = sample->ConvertToContiguousBuffer(&buffer);
				if (SUCCEEDED(hr)) {
					BYTE* data;
					DWORD pcbMaxLength = 0, pcbCurrentLength = 0;
					hr = buffer->Lock(&data, &pcbMaxLength, &pcbCurrentLength);

					char text[256];
					sprintf_s(text, "hr 0x%08X, pcbCurrentLength %u\n", hr, pcbCurrentLength);
					OutputDebugStringA(text);

					static HANDLE g_File = nullptr;
					if(!g_File) 
						g_File = CreateFileW(L"C:\\Temp\\Output.h264", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
					WriteFile(g_File, data, pcbCurrentLength, nullptr, nullptr);

					if (SUCCEEDED(hr)) {
						send(s, (const char*)data, pcbCurrentLength, 0);
						hr = buffer->Unlock();
					}
					buffer->Release();
					buffer = NULL;
				}
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
			}

			sample->Release();
			sample = NULL;
			if (buffer != NULL) {
				buffer->Release();
				buffer = NULL;
			}
			if (buffer != NULL) {
				buffer->Release();
				buffer = NULL;
			}

		}
		});

	thread.detach();


	while (true) {
		hr = dxgiOutputDuplication->AcquireNextFrame(300, &frame_info, &resource);
		if (SUCCEEDED(hr)) {

			ID3D11Texture2D* texture = NULL;

			hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
			if (SUCCEEDED(hr)) {
				D3D11_TEXTURE2D_DESC desc1;
				texture->GetDesc(&desc1);
				desc1.Usage = D3D11_USAGE_DEFAULT;
				desc1.MiscFlags = 0;
				desc1.BindFlags = 0;
				desc1.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				ID3D11Texture2D* texture1 = NULL;
				hr = device->CreateTexture2D(&desc1, NULL, &texture1);
				ctx->CopyResource(texture1, texture);
				texture->Release();
				texture = NULL;
				texture1 = to_format(texture1, DXGI_FORMAT_NV12);
				if (texture1 != NULL) {
					//texture->Release();
					//texture = NULL;
					//ctx->CopyResource(texture1, texture);

					IDXGISurface* dxgiSurface1 = NULL;

					hr = texture1->QueryInterface(__uuidof(IDXGISurface), (void**)&dxgiSurface1);
					if (SUCCEEDED(hr)) {

						IMFSample* sample = NULL;
						hr = MFCreateSample(&sample);
						if (SUCCEEDED(hr)) {
							IMFMediaBuffer* buffer = NULL;
							hr = MFCreateDXGISurfaceBuffer(_uuidof(ID3D11Texture2D), dxgiSurface1, 0, FALSE, &buffer);
							if (SUCCEEDED(hr)) {
								hr = sample->AddBuffer(buffer);
								hr = sample->SetSampleDuration(duration);
								hr = presentation->GetTime(&sampleTime);
								hr = sample->SetSampleTime(sampleTime);

								static int64_t g_Time = 0;
								auto const Time = g_Time;
								g_Time += 50'0000;
								hr = sample->SetSampleTime(Time);
								hr = sample->SetSampleDuration(50'0000);

								DWORD length = 0;
								IMF2DBuffer* imf_buffer = NULL;
								hr = buffer->QueryInterface(__uuidof(IMF2DBuffer), (void**)&imf_buffer);
								if (SUCCEEDED(hr)) {
									hr = imf_buffer->GetContiguousLength(&length);
									hr = buffer->SetCurrentLength(length);
									hr = transform->ProcessInput(0, sample, 0);

									char text[256];
									sprintf_s(text, "hr 0x%08X\n", hr);
									OutputDebugStringA(text);

									imf_buffer->Release();
									imf_buffer = NULL;
									buffer->Release();
									buffer = NULL;
								}
								sample->Release();
								sample = NULL;
							}
						}
						dxgiSurface1->Release();
						dxgiSurface1 = NULL;
					}
					texture1->Release();
					texture1 = NULL;
				}
			}
			resource->Release();
			resource = NULL;
		}
		dxgiOutputDuplication->ReleaseFrame();

		presentation->GetTime(&sampleTime);

		auto min = sampleTime - last_time;

		last_time = sampleTime;

		min = duration - min;

		if (min > 0) {
			std::this_thread::sleep_for(std::chrono::nanoseconds(duration - min) * 10 * 10);
		}

		//std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
}
ID3D11Texture2D* to_format(ID3D11Texture2D* texture, DXGI_FORMAT format) {
	if (texture == NULL) {
		return NULL;
	}
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);

	D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{ 0,D3D11_VPIV_DIMENSION_TEXTURE2D };
	ID3D11VideoProcessorInputView* inputView = NULL;
	ID3D11VideoProcessorOutputView* outputView = NULL;

	HRESULT hr = S_OK;

	hr = videoDevice->CreateVideoProcessorInputView(texture, vpe, &inputViewDesc, &inputView);
	if (FAILED(hr)) {
		return NULL;
	}
	desc.CPUAccessFlags = 0;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.Format = format;

	ID3D11Texture2D* texture1 = NULL;
	hr = device->CreateTexture2D(&desc, NULL, &texture1);
	if (FAILED(hr)) {
		inputView->Release();
		return NULL;
	}
	D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{ D3D11_VPOV_DIMENSION_TEXTURE2D };
	hr = videoDevice->CreateVideoProcessorOutputView(texture1, vpe, &outputViewDesc, &outputView);
	if (FAILED(hr)) {
		inputView->Release();
		texture1->Release();
		return NULL;
	}
	D3D11_VIDEO_PROCESSOR_STREAM st{ TRUE };
	st.pInputSurface = inputView;
	hr = videoContext->VideoProcessorBlt(processor, outputView, 0, 1, &st);
	if (FAILED(hr)) {
		inputView->Release();
		outputView->Release();
		texture1->Release();
		return NULL;
	}
	inputView->Release();
	outputView->Release();
	return texture1;
}