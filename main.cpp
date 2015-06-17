// include the basic windows header files and the Direct3D header files
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include <vector>

//helper class/functions for D3D12 taken from documentation pages
#include "helpers.h"
#include "textureloader.h"

#include <SDL.h>
#undef main


// global declarations
const UINT g_bbCount = 4; //define number of backbuffers to use
Microsoft::WRL::ComPtr<ID3D12Device> mDevice;					//d3d12 device
Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandListAllocator; //d3d12 command list allocator
Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue; //d3d12 command queue
Microsoft::WRL::ComPtr<IDXGIDevice2> mDXGIDevice; //DXGI device
Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;   // the pointer to the swap chain interface
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;  //d3d12 command list
Microsoft::WRL::ComPtr<ID3D12Fence> mFence; //fence used by GPU to signal when command queue execution has finished
Microsoft::WRL::ComPtr<ID3D12Resource> mRenderTarget[g_bbCount]; //backbuffer resource, like d3d11's ID3D11Texture2D, array of 2 for flip_sequential support
CDescriptorHeapWrapper mRTVDescriptorHeap; //descriptor heap wrapper class instance, for managing RTV descriptor heap
D3D12_VIEWPORT mViewPort; //viewport, same as d3d11
RECT mRectScissor;
HANDLE mHandle; //fired by the fence when the GPU signals it, CPU waits on this event handle
Shader g_VS;
Shader g_PS;
RootSignature g_RootSig;
PipelineStateObject g_PSO;
VertexBufferResource g_VB;

//Constant buffer resources, mapped pointers, and descriptor heap for view/proj CBVs
CUploadBufferWrapper mWorldMatrix;
CUploadBufferWrapper mViewMatrix;
CUploadBufferWrapper mProjMatrix;
CDescriptorHeapWrapper mCBDescriptorHeap;

//texture support
Microsoft::WRL::ComPtr<ID3D12Resource> mTexture2D; //default heap resource, GPU will copy texture resource to these from upload buffer
CDescriptorHeapWrapper mSamplerHeap;

//Fullscreen support
HWND g_hWnd;
BOOL g_requestResize = false;





									   // function prototypes
void InitD3D(HWND hWnd);    // sets up and initializes Direct3D
void CleanD3D(void);        // closes Direct3D and releases memory
void Frame();				// called once per frame to build then execute command list, and then present frame
void WaitForCommandQueueFence(); //function called by command queue after executing command list, blocks CPU thread until GPU signals mFence
HRESULT ResizeSwapChain(); //resizes the swapchain buffers to the client window size, recreates the RTVs

/*
							// the WindowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


// the entry point for any Windows program
int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	HWND hWnd;
	WNDCLASSEX wc;

	ZeroMemory(&wc, sizeof(WNDCLASSEX));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszClassName = "WindowClass";

	RegisterClassEx(&wc);

	RECT wr = { 0, 0, 800, 600 };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	hWnd = CreateWindowEx(NULL,
		"WindowClass",
		"Our First D3D12 Program",
		WS_OVERLAPPEDWINDOW,
		300,
		100,
		wr.right - wr.left,
		wr.bottom - wr.top,
		NULL,
		NULL,
		hInstance,
		NULL);

	g_hWnd = hWnd;

	ShowWindow(hWnd, nCmdShow);

	// set up and initialize Direct3D
	InitD3D(hWnd);

	// enter the main loop:

	MSG msg;

	while (TRUE)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
				break;
		}
		else
		{
			Frame();
			// Run game code here
			// ...
			// ...
		}
	}

	// clean up DirectX and COM
	CleanD3D();

	return (int)msg.wParam;
}


// this is the main message handler for the program
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
	case WM_SIZE:
		{
			g_requestResize = true;
		}
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}*/


// this function initializes and prepares Direct3D for use
void InitD3D(HWND hWnd)
{
	//This example shows calling D3D12CreateDevice to create the device.
	HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
			__uuidof(ID3D12Device), (void**)&mDevice);

	D3D12_FEATURE_DATA_D3D12_OPTIONS options;
	if (FAILED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, reinterpret_cast<void*>(&options),
		sizeof(options))))
	{
		throw;
	}
	
	//Create the command allocator and queue objects.Then, obtain command lists from the command allocator and submit them to the command queue.
	//This example shows calling ID3D12Device::CreateCommandAllocator and ID3D12Device::GetDefaultCommandQueue. Latter doesn't exist tho??
	hr = mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&mCommandListAllocator);
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = mDevice->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), (void**)&mCommandQueue);

	//Create the swap chain similarly to how it was done in Direct3D 11.

	// Create the swap chain descriptor.
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = g_bbCount;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.Flags = 0; //DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	//swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

	
	// Get the DXGI factory used to create the swap chain.
	IDXGIFactory2 *dxgiFactory = nullptr;
	hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)&dxgiFactory);


	// Create the swap chain using the command queue, NOT using the device.  Thanks Killeak!
	hr = dxgiFactory->CreateSwapChain(mCommandQueue.Get(), &swapChainDesc, (IDXGISwapChain**)mSwapChain.GetAddressOf());

	//increase max frame latency when required
	if (swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
	{
		mSwapChain->SetMaximumFrameLatency(g_bbCount);
	}

	dxgiFactory->Release();

	//Create the RTV descriptor heap with g_bbCount entries (front and back buffer since swapchain resource rotation is no longer automatic).
	//Documentation recommends flip_sequential for D3D12, see - https://msdn.microsoft.com/en-us/library/windows/desktop/dn903945
	mRTVDescriptorHeap.Create(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_bbCount);

	//RTV descriptor and viewport/scissor initialization moved to seperate function
	hr = ResizeSwapChain();

	
	//create worldMatrix buffer resource on it's own heap and leave mapped, the end-of-frame fence prevents
	//it being written while the GPU is still reading it.
	UINT cbSize = sizeof(DirectX::XMMATRIX);
	hr = mWorldMatrix.Create(mDevice.Get(), cbSize, D3D12_HEAP_TYPE_UPLOAD);

	//create viewMatrix buffer resource on it's own heap and leave mapped
	hr = mViewMatrix.Create(mDevice.Get(), cbSize, D3D12_HEAP_TYPE_UPLOAD);

	//create projMatrix buffer resource on it's own heap and leave mapped
	hr = mProjMatrix.Create(mDevice.Get(), cbSize, D3D12_HEAP_TYPE_UPLOAD);

	//create the descriptor heap for the view and proj matrix CB views (and now a texture2d SRV view also)
	mCBDescriptorHeap.Create(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, true);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbDesc;
	//create the view matrix constant buffer view descriptor
	cbDesc.BufferLocation = mViewMatrix.pBuf->GetGPUVirtualAddress();
	cbDesc.SizeInBytes = 256;// max(cbSize, 256); //Size of 64 is invalid.  Device requires SizeInBytes be a multiple of 256. [ STATE_CREATION ERROR #645 ]
	mDevice->CreateConstantBufferView(&cbDesc, mCBDescriptorHeap.hCPU(0));

	//create the proj matrix constant buffer view descriptor
	cbDesc.BufferLocation = mProjMatrix.pBuf->GetGPUVirtualAddress();
	cbDesc.SizeInBytes = 256; //CB descriptor size must be multiple of 256
	mDevice->CreateConstantBufferView(&cbDesc, mCBDescriptorHeap.hCPU(1));
	
	//changed shader compile target to HLSL 5.0
	g_VS.Load("Shaders.hlsl", "VSMain", "vs_5_0");
	g_PS.Load("Shaders.hlsl", "PSMain", "ps_5_0");
	//changed root sig function to include 2 root parameters: A root CBV of worldmatrix, 
	//and a two entry descriptor table for view and proj matrix CBVs
	g_RootSig.Create(mDevice.Get());

	
	VertexTypes::P3F_T2F triangleVerts[] =
	{
		{ 0.0f, 0.5f, 0.0f, { 0.5f, 0.0f } },
		{ 0.45f, -0.5, 0.0f, { 1.0f, 1.0f } },
		{ -0.45f, -0.5f, 0.0f, { 0.0f, 1.0f } }
	};

	g_VB.Create(
		mDevice.Get(), sizeof(triangleVerts), 
		sizeof(VertexTypes::P3F_T2F), triangleVerts);


	g_PSO.Create(
		mDevice.Get(),
		PipelineStateObjectDescription::Simple(
			VertexTypes::P3F_T2F::GetInputLayoutDesc(),
			g_RootSig,
			g_VS, g_PS
		));

	//With the command list allocator and a PSO, you can create the actual command list, which will be executed at a later time.
	//This example shows calling ID3D12Device::CreateCommandList.
	hr = mDevice->CreateCommandList(
		1, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		mCommandListAllocator.Get(), g_PSO.Get(), 
		__uuidof(ID3D12CommandList), (void**)&mCommandList);
	ThrowIfFailed(hr);

	//create a GPU fence that will fire an event once the command list has been executed by the command queue.
	mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&mFence);
	//And the CPU event that the fence will fire off
	mHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);


	//Define the viewport that you will be rendering to.This example shows how to set the viewport to the same size as the Win32 window client.Also note that mViewPort is a member variable.
	//Whenever a command list is reset, you must attach the view port state to the command list before the command list is executed.
	//mViewPort will let you do so without needing to redefine it every frame.

	//create a sampler descriptor heap and a valid sampler descriptor on it.
	mSamplerHeap.Create(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
	D3D12_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(samplerDesc));
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	mDevice->CreateSampler(&samplerDesc, mSamplerHeap.hCPU(0));

	//create an upload buffer that the CPU can copy data into 
	CUploadBufferWrapper textureUploadBuffer;
	UINT bufferSize = 1024 * 1024 * 10; //10MB
	hr = textureUploadBuffer.Create(mDevice.Get(), bufferSize, D3D12_HEAP_TYPE_UPLOAD);

	//loads dds texture file into an upload buffer then issues a command for the GPU to copy it to a default resource. 
	//See CreateD3DResources in textureloader.h for the details.
	hr = CreateTexture2D(mDevice.Get(), mCommandList.Get(), &textureUploadBuffer, L"seafloor2.dds", mTexture2D.GetAddressOf());
	
	// Transition the texture resource to a generic read state.
	setResourceBarrier(mCommandList.Get(), mTexture2D.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

	//The commandlist now contains the uploadbuffer-to-default-resource copy command, as well as the barrier
	//to transition the default resource to a generic read state.  Those operations must be executed before the resource
	//is ready for use in the render loop.
	mCommandList->Close();
	mCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)mCommandList.GetAddressOf());

	//create an SRV descriptor of the texture in the same descriptor heap as the CBV descriptors. 
	D3D12_RESOURCE_DESC resDesc = mTexture2D->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = resDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = resDesc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	mDevice->CreateShaderResourceView(mTexture2D.Get(), &srvDesc, mCBDescriptorHeap.hCPU(2)); //index location is 2 since slots 0 and 1 contain CBVs.

	//wait for GPU to signal it has finished processing the queued command list(s).
	WaitForCommandQueueFence();

	// Command list allocators can be only be reset when the associated command lists have finished execution on the GPU; 
	// apps should use fences to determine GPU execution progress.
	hr = mCommandListAllocator->Reset();
	hr = mCommandList->Reset(mCommandListAllocator.Get(), NULL);

}

void Frame()
{
	using namespace DirectX;

	HRESULT hr;

	if (g_requestResize) ResizeSwapChain();

	//rotation in radians of 0-90, 270-360 degrees (skip backfacing angles)
	static float angle = 0.0f;
	angle += XM_PI / 180.0f;
	if ((angle > XM_PI * 0.5f) && (angle < XM_PI * 1.5f)) angle = XM_PI * 1.5f;
	if (angle > XM_2PI) angle = 0.0f;

	//rotate worldmatrix around Y, transpose, and copy to the persistently mapped upload heap resource of the worldmatrix buffer
	XMMATRIX rotated = XMMatrixIdentity();
	rotated = XMMatrixRotationY(angle);
	rotated = XMMatrixTranspose(rotated);
	memcpy(mWorldMatrix.pDataBegin, &rotated, sizeof(rotated));

	//writing the view/proj buffers every frame, even tho they don't change in this example.
	//build and copy viewmatrix to the persistently mapped viewmatrix buffer
	XMVECTOR eye { 0.0f, 0.0f, -2.0f, 0.0f };
	XMVECTOR eyedir { 0.0f, 0.0f, 0.0f, 0.0f };
	XMVECTOR updir { 0.0f, 1.0f, 0.0f, 0.0f };
	XMMATRIX view = XMMatrixLookAtLH(eye, eyedir, updir);
	view = XMMatrixTranspose(view);
	memcpy(mViewMatrix.pDataBegin, &view, sizeof(view));

	//build and copy projection matrix to the persistently mapped projmatrix buffer
	XMMATRIX proj = XMMatrixPerspectiveFovLH((XM_PI / 4.0f), (6.0f / 8.0f), 0.1f, 100.0f);
	proj = XMMatrixTranspose(proj);
	memcpy(mProjMatrix.pDataBegin, &proj, sizeof(proj));
	
	//Get the index of the active back buffer from the swapchain
	UINT backBufferIndex = 0;
	backBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
	

	//get active RTV from descriptor heap wrapper class by index
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = mRTVDescriptorHeap.hCPU(backBufferIndex);
	

	//Now, reuse the command list for the current frame.
	//Reattach the viewport to the command list, indicate that the resource will be in use as a render target, record commands, and then 
	//indicate that the render target will be used to present when the command list is done executing.

	//This example shows calling ID3D12GraphicsCommandList::ResourceBarrier to indicate to the system that you are about to use a resource.
	//Resource barriers are used to handle multiple accesses to a resource(refer to the Remarks for ResourceBarrier).
	//You have to explicitly state that mRenderTarget is about to be changed from being "used to present" to being "used as a render target".
	mCommandList->RSSetViewports(1, &mViewPort);
	mCommandList->RSSetScissorRects(1, &mRectScissor);
	mCommandList->SetPipelineState(g_PSO.Get());
	mCommandList->SetGraphicsRootSignature(g_RootSig.Get());

	//set the root CBV of the worldmatrix and the root descriptor table containing the view and proj matrices' view descriptors
	mCommandList->SetGraphicsRootConstantBufferView(0, mWorldMatrix.pBuf->GetGPUVirtualAddress());
	ID3D12DescriptorHeap* pHeaps[2] = { mCBDescriptorHeap.pDH.Get(), mSamplerHeap.pDH.Get() };
	mCommandList->SetDescriptorHeaps(2, pHeaps); //this call IS necessary
	mCommandList->SetGraphicsRootDescriptorTable(1, mCBDescriptorHeap.hGPUHeapStart);
	//set the SRV and sampler tables
	mCommandList->SetGraphicsRootDescriptorTable(2, mCBDescriptorHeap.hGPU(2)); //the single SRV was put on the end of the CB heap
	mCommandList->SetGraphicsRootDescriptorTable(3, mSamplerHeap.hGPUHeapStart);

	// Indicate that this resource will be in use as a render target.
	setResourceBarrier(mCommandList.Get(), mRenderTarget[backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	
	// Record commands.
	float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	mCommandList->ClearRenderTargetView(rtv, clearColor, NULL, 0);
	mCommandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->IASetVertexBuffers(0, 1, &g_VB.GetView());
	mCommandList->DrawInstanced(3, 1, 0, 0);

	// Indicate that the render target will now be used to present when the command list is done executing.
	setResourceBarrier(mCommandList.Get(), mRenderTarget[backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// Execute the command list.
	hr = mCommandList->Close();
	mCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)mCommandList.GetAddressOf());


	// Swap the back and front buffers.
	DXGI_PRESENT_PARAMETERS params;
	ZeroMemory(&params, sizeof(params));
	//hr = mSwapChain->Present1(0, DXGI_PRESENT_DO_NOT_WAIT, &params);
	hr = mSwapChain->Present(0, 0);

	//wait for GPU to signal it has finished processing the queued command list(s).
	WaitForCommandQueueFence();


	// Command list allocators can be only be reset when the associated command lists have finished execution on the GPU; 
	// apps should use fences to determine GPU execution progress.
	hr = mCommandListAllocator->Reset();
	hr = mCommandList->Reset(mCommandListAllocator.Get(), NULL);
}

//Assigns an event to mFence, and sets the fence's completion signal value. 
//Then asks the GPU to signal that fence, and asks the CPU to wait for the event handle.
void WaitForCommandQueueFence()
{
	//reset the fence signal
	mFence->Signal(0);
	//set the event to be fired once the signal value is 1
	mFence->SetEventOnCompletion(1, mHandle); 

	//after the command list has executed, tell the GPU to signal the fence
	mCommandQueue->Signal(mFence.Get(), 1);

	//wait for the event to be fired by the fence
	WaitForSingleObject(mHandle, INFINITE);
}

HRESULT ResizeSwapChain()
{
	HRESULT hr = S_OK;
	g_requestResize = false;

	//get the client window area size.
	RECT clientSize;
	UINT width, height;
	GetClientRect(g_hWnd, &clientSize);
	width = clientSize.right;
	height = clientSize.bottom;

	//if the client size is valid (ignore minimize etc).
	if (width > 0)
	{

		//release existing backbuffer resources pointers (use reset for ComPtr rather than release, or just assign nullptr).
		for (UINT i = 0; i < g_bbCount; ++i)
		{
			mRenderTarget[i].Reset();
		}

		//resize the swapchain buffers
		mSwapChain->ResizeBuffers(g_bbCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0); //DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

		//A buffer is required to render to.This example shows how to create that buffer by using the swap chain and device.
		//This example shows calling ID3D12Device::CreateRenderTargetView.
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
		ZeroMemory(&rtvDesc, sizeof(rtvDesc));
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		//loop for all backbuffer resources
		for (UINT i = 0; i < g_bbCount; ++i)
		{
			hr = mSwapChain->GetBuffer(i, __uuidof(ID3D12Resource), (LPVOID*)&mRenderTarget[i]);
			mRenderTarget[0]->SetName(L"mRenderTarget" + i);  //set debug name 
			mDevice->CreateRenderTargetView(mRenderTarget[i].Get(), &rtvDesc, mRTVDescriptorHeap.hCPU(i));
		}

		//fill out a viewport struct
		ZeroMemory(&mViewPort, sizeof(D3D12_VIEWPORT));
		mViewPort.TopLeftX = 0;
		mViewPort.TopLeftY = 0;
		mViewPort.Width = (float)width;
		mViewPort.Height = (float)height;
		mViewPort.MinDepth = 0.0f;
		mViewPort.MaxDepth = 1.0f;

		mRectScissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	}

	return hr;
}


// this is the function that cleans up Direct3D and COM
void CleanD3D(void)
{
	//ensure we're not fullscreen
	mSwapChain->SetFullscreenState(FALSE, NULL);

	//close the event handle so that mFence can actually release()
	CloseHandle(mHandle);
}

void main(int argc, char *args[]) {
	SDL_Window* window = SDL_CreateWindow("DirectX 12 Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, 0);

	g_hWnd = GetActiveWindow();
	InitD3D(g_hWnd);

	while (TRUE)
	{
		SDL_Event windowEvent;
		if (SDL_PollEvent(&windowEvent)) {
			if (windowEvent.type == SDL_QUIT) break;
		}

		Frame();
	}

	CleanD3D();
}