#pragma once

#include <assert.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <string>
#include <stdint.h>
#include <tuple>

//#include "DDSTextureLoader\DDSTextureLoader.h"

#pragma comment (lib, "d3d12.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "d3dcompiler.lib")

#define ThrowIfFailed(expr) \
	do { \
		if (FAILED(expr)) throw; \
	} while (0);

//taken from https://msdn.microsoft.com/en-us/library/windows/desktop/dn859359(v=vs.85).aspx
//helper class for managing descriptor heaps
class CDescriptorHeapWrapper
{
public:
	CDescriptorHeapWrapper() { memset(this, 0, sizeof(*this)); }

	HRESULT Create(
		ID3D12Device* pDevice,
		D3D12_DESCRIPTOR_HEAP_TYPE Type,
		UINT NumDescriptors,
		bool bShaderVisible = false)
	{
		Desc.Type = Type;
		Desc.NumDescriptors = NumDescriptors;
		Desc.Flags = (bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : (D3D12_DESCRIPTOR_HEAP_FLAGS)0);

		HRESULT hr = pDevice->CreateDescriptorHeap(&Desc,
			__uuidof(ID3D12DescriptorHeap),
			(void**)&pDH);
		if (FAILED(hr)) return hr;

		hCPUHeapStart = pDH->GetCPUDescriptorHandleForHeapStart();
		if (bShaderVisible)
		{
			hGPUHeapStart = pDH->GetGPUDescriptorHandleForHeapStart();
		}
		else
		{
			hGPUHeapStart.ptr = 0;
		}
		HandleIncrementSize = pDevice->GetDescriptorHandleIncrementSize(Desc.Type);
		return hr;
	}
	operator ID3D12DescriptorHeap*() { return pDH.Get(); }

	SIZE_T MakeOffsetted(SIZE_T ptr, UINT index)
	{
		SIZE_T offsetted;
		offsetted = ptr + index * HandleIncrementSize;
		return offsetted;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE hCPU(UINT index)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = MakeOffsetted(hCPUHeapStart.ptr, index);
		return handle;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE hGPU(UINT index)
	{
		assert(Desc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
		D3D12_GPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = MakeOffsetted(hGPUHeapStart.ptr, index);
		return handle;
	}
	D3D12_DESCRIPTOR_HEAP_DESC Desc;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pDH;
	D3D12_CPU_DESCRIPTOR_HANDLE hCPUHeapStart;
	D3D12_GPU_DESCRIPTOR_HANDLE hGPUHeapStart;
	UINT HandleIncrementSize;
};



//helper function for resource barriers in command lists
void setResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, UINT StateBefore, UINT StateAfter)
{
	D3D12_RESOURCE_BARRIER barrierDesc = {};

	barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrierDesc.Transition.pResource = resource;
	barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrierDesc.Transition.StateBefore = (D3D12_RESOURCE_STATES)StateBefore;
	barrierDesc.Transition.StateAfter = (D3D12_RESOURCE_STATES)StateAfter;

	commandList->ResourceBarrier(1, &barrierDesc);
}

class CUploadBufferWrapper
{
public:
	HRESULT Create(ID3D12Device* device, const SIZE_T size, D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS miscFlag = D3D12_HEAP_FLAG_NONE)
	{
		HRESULT hr;

		D3D12_HEAP_PROPERTIES heapProps;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.Type = heapType;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC bufferDesc;
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Alignment = 0;
		bufferDesc.Width = static_cast<UINT64>(size);
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.SampleDesc.Quality = 0;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		//create buffer resource on it's own individual upload heap
		hr = device->CreateCommittedResource(
			&heapProps,
			miscFlag,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			__uuidof(ID3D12Resource),
			(void**)pBuf.GetAddressOf());

		if (hr == S_OK)
		{
			//map the buffer
			pBuf->Map(0, nullptr, (void**)&pDataBegin);
			pDataCur = pDataBegin;
			pDataEnd = pDataBegin + size;
		}

		return hr;
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> pBuf;
	UINT8* pDataBegin;
	UINT8* pDataCur;
	UINT8* pDataEnd;

};

static std::wstring StringToWString(const std::string& str)
{
	std::wstring result(str.begin(), str.end());
	return result;
}

namespace VertexTypes
{
	struct P3F_C4F
	{
		float pos[3];
		float color[4];

		static const D3D12_INPUT_LAYOUT_DESC& GetInputLayoutDesc()
		{
			static D3D12_INPUT_ELEMENT_DESC elements[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};
			static D3D12_INPUT_LAYOUT_DESC desc = { elements, sizeof(elements) / sizeof(elements[0]) };
			return desc;
		}
	};

	struct P3F_T2F
	{
		float pos[3];
		float tex[2];
		static const D3D12_INPUT_LAYOUT_DESC& GetInputLayoutDesc()
		{
			static D3D12_INPUT_ELEMENT_DESC elements[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};
			static D3D12_INPUT_LAYOUT_DESC desc = { elements, sizeof(elements) / sizeof(elements[0]) };
			return desc;
		}
	};
}

class RootSignature
{
public:
	void Create(ID3D12Device* device)
	{
		D3D12_ROOT_SIGNATURE_DESC descRootSignature = D3D12_ROOT_SIGNATURE_DESC();
		descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		//declare 4 root parameters in root sig
		descRootSignature.NumParameters = 4;
		//create an array that will describe each root parameter
		D3D12_ROOT_PARAMETER rootParams[4];
		descRootSignature.pParameters = rootParams; //set param array in the root sig
		
		//added a CBV to API slot 0 of this root signature, uses 4 of the 16 dwords available. (https://msdn.microsoft.com/en-us/library/dn899209(v=vs.85).aspx)
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].Descriptor.ShaderRegister = 0;

		//create an array of descriptor ranges, these range(s) form the entries in descriptor tables 
		D3D12_DESCRIPTOR_RANGE descRange[3];
		//CBV range
		descRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		descRange[0].NumDescriptors = 2;
		descRange[0].BaseShaderRegister = 1;
		descRange[0].RegisterSpace = 0;
		descRange[0].OffsetInDescriptorsFromTableStart = 0;
		//SRV range
		descRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descRange[1].NumDescriptors = 1;
		descRange[1].BaseShaderRegister = 0;
		descRange[1].RegisterSpace = 0;
		descRange[1].OffsetInDescriptorsFromTableStart = 0;
		//sampler range
		descRange[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		descRange[2].NumDescriptors = 1;
		descRange[2].BaseShaderRegister = 0;
		descRange[2].RegisterSpace = 0;
		descRange[2].OffsetInDescriptorsFromTableStart = 0;

		//added a descriptor table with 2 CBVs to slot 1 of root sig, uses 1 dword
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &descRange[0];
		//table with 1 SRV to slot 2 of the root sig, uses 1 dword
		rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[2].DescriptorTable.pDescriptorRanges = &descRange[1];
		//added sampler descriptor table at slot 3 of root sig, costs 1 dword.
		rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[3].DescriptorTable.pDescriptorRanges = &descRange[2];
		//end of root sig, 7/16 dwords used

		ThrowIfFailed(
			D3D12SerializeRootSignature(
				&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1,
				m_Blob.GetAddressOf(), m_ErrorBlob.GetAddressOf())
			);
		ThrowIfFailed(
			device->CreateRootSignature(
				1, m_Blob->GetBufferPointer(), m_Blob->GetBufferSize(),
				__uuidof(ID3D12RootSignature), (void**)m_RootSignature.GetAddressOf())
			);
	}

	auto Get() const { return m_RootSignature.Get(); }
	auto GetBlob() const { return m_Blob.Get(); }
	auto GetErrorBlob() const { return m_ErrorBlob.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
	Microsoft::WRL::ComPtr<ID3DBlob> m_Blob;
	Microsoft::WRL::ComPtr<ID3DBlob> m_ErrorBlob;
};

class Shader
{
public:
	void Load(const char* filename, const char* entryPoint, const char* target)
	{
		HRESULT hr = D3DCompileFromFile(
			StringToWString(filename).c_str(), nullptr , nullptr,
			entryPoint, target, D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
			m_Blob.GetAddressOf(), m_ErrorBlob.GetAddressOf());

		if (m_ErrorBlob.Get())
		{
			OutputDebugStringA(
				reinterpret_cast<LPCSTR>(m_ErrorBlob.Get()->GetBufferPointer())
				);
		}
		ThrowIfFailed(hr);

	}

	auto GetBlob() const { return m_Blob.Get(); }
	auto GetErrorBlob() const { return m_ErrorBlob.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3DBlob> m_Blob;
	Microsoft::WRL::ComPtr<ID3DBlob> m_ErrorBlob;
};

struct PipelineStateObjectDescription : D3D12_GRAPHICS_PIPELINE_STATE_DESC
{
	static PipelineStateObjectDescription Simple(
		const D3D12_INPUT_LAYOUT_DESC& inputLayout,
		const RootSignature& rootSig,
		const Shader& vs, const Shader& ps
		)
	{
		ID3DBlob* vsBlob = vs.GetBlob();
		ID3DBlob* psBlob = ps.GetBlob();
		PipelineStateObjectDescription psoDesc;
		ZeroMemory(&psoDesc, sizeof(psoDesc));
		psoDesc.InputLayout = inputLayout;
		psoDesc.pRootSignature = rootSig.Get();
		psoDesc.VS = { reinterpret_cast<BYTE*>(vsBlob->GetBufferPointer()), vsBlob->GetBufferSize() };
		psoDesc.PS = { reinterpret_cast<BYTE*>(psBlob->GetBufferPointer()), psBlob->GetBufferSize() };

		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
		psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthClipEnable = TRUE;
		psoDesc.RasterizerState.MultisampleEnable = FALSE;
		psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
		psoDesc.RasterizerState.ForcedSampleCount = 0;
		psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
		psoDesc.BlendState.IndependentBlendEnable = FALSE;
		const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
		{
			FALSE,FALSE,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};
		for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			psoDesc.BlendState.RenderTarget[i] = defaultRenderTargetBlendDesc;
		}

		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		psoDesc.SampleDesc.Count = 1;

		//psoDesc.RasterizerState.DepthClipEnable = false;

		return psoDesc;
	}
};

class PipelineStateObject
{
public:
	void Create(
		ID3D12Device* device,
		const PipelineStateObjectDescription& desc)
	{
		ThrowIfFailed(
			device->CreateGraphicsPipelineState(
				&desc, 
				__uuidof(ID3D12PipelineState), 
				(void**)m_PSO.GetAddressOf())
			);
	}

	auto Get() const { return m_PSO.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PSO;
};

class CommittedResource
{
protected:
	void _Create(
		ID3D12Device* device, 
		int64_t size,
		_In_opt_ const void* data)
	{
		D3D12_HEAP_PROPERTIES heapProps;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC bufferDesc;
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Alignment = 0;
		bufferDesc.Width = static_cast<UINT64>(size);
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.SampleDesc.Quality = 0;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		m_Size = size;
		ThrowIfFailed(
			device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(m_Resource.GetAddressOf()))
			);
		
		if (data)
		{
			UploadData(data, size);
		}
	}

public:
	void UploadData(const void* data, int64_t size)
	{
		UINT8* dataBegin;
		ThrowIfFailed(
			m_Resource->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin))
			);
		memcpy(dataBegin, data, static_cast<size_t>(size));
		m_Resource->Unmap(0, nullptr);
	}

	auto Get() const { return m_Resource.Get(); }
	auto GetSize() const { return m_Size; }

protected:
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
	int64_t m_Size;
};

class VertexBufferResource : public CommittedResource
{
public:
	void Create(
		ID3D12Device* device,
		int32_t sizeInBytes,
		int32_t strideInBytes,
		_In_opt_ const void* data)
	{
		m_Stride = strideInBytes;
		_Create(device, sizeInBytes, data);
		m_View.BufferLocation = m_Resource->GetGPUVirtualAddress();
		m_View.SizeInBytes = sizeInBytes;
		m_View.StrideInBytes = strideInBytes;
	}

	auto GetStride() const { return m_Stride; }
	const auto& GetView() const { return m_View; }

private:
	int32_t m_Stride;
	D3D12_VERTEX_BUFFER_VIEW m_View;
};

