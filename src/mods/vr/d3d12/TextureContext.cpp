#include <utility/String.hpp>
#include <utility/Logging.hpp>

#include <spdlog/spdlog.h>

#include "CommandContext.hpp"
#include "TextureContext.hpp"
#include "render/D3D12Diagnostics.hpp"

namespace d3d12 {
bool TextureContext::setup(ID3D12Device* device, ID3D12Resource* rsrc, std::optional<DXGI_FORMAT> rtv_format, std::optional<DXGI_FORMAT> srv_format, const wchar_t* name) {
    SPDLOG_INFO_EVERY_N_SEC(1, "Setting up texture context for {}", utility::narrow(name));
    
    reset();

    commands.setup(name);

    texture.Reset();
    texture = rsrc;

    if (rsrc == nullptr) {
        return false;
    }

    rsrc->SetName(name);
    render::D3D12Diagnostics::get().register_resource("VR::TextureContext::setup", rsrc, true, utility::narrow(name));

    return create_rtv(device, rtv_format) && create_srv(device, srv_format);
}

bool TextureContext::create_rtv(ID3D12Device* device, std::optional<DXGI_FORMAT> format) {
    SPDLOG_INFO_EVERY_N_SEC(1, "Creating RTV for texture context");

    rtv_heap.reset();

    // create descriptor heap
    try {
        rtv_heap = std::make_unique<DirectX::DescriptorHeap>(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            1);
    } catch(...) {
        spdlog::error("Failed to create RTV descriptor heap");
        return false;
    }

    if (rtv_heap->Heap() == nullptr) {
        return false;
    }

    render::D3D12Diagnostics::get().register_descriptor_heap("VR::TextureContext::create_rtv", rtv_heap->Heap(), 1, true, "TextureContext RTV Heap");

    if (format) {
        const auto desc = texture->GetDesc();
        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
        rtv_desc.Format = (DXGI_FORMAT)*format;
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize > 1) {
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv_desc.Texture2DArray.MipSlice = 0;
            rtv_desc.Texture2DArray.FirstArraySlice = 0;
            rtv_desc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
            rtv_desc.Texture2DArray.PlaneSlice = 0;
        } else {
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Texture2D.MipSlice = 0;
            rtv_desc.Texture2D.PlaneSlice = 0;
        }
        device->CreateRenderTargetView(texture.Get(), &rtv_desc, get_rtv());
    } else {
        device->CreateRenderTargetView(texture.Get(), nullptr, get_rtv());
    }

    return true;
}

bool TextureContext::create_srv(ID3D12Device* device, std::optional<DXGI_FORMAT> format) {
    SPDLOG_INFO_EVERY_N_SEC(1, "Creating SRV for texture context");

    srv_heap.reset();

    // create descriptor heap
    try {
        srv_heap = std::make_unique<DirectX::DescriptorHeap>(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            1);
    } catch(...) {
        spdlog::error("Failed to create SRV descriptor heap");
        return false;
    }

    if (srv_heap->Heap() == nullptr) {
        return false;
    }

    render::D3D12Diagnostics::get().register_descriptor_heap("VR::TextureContext::create_srv", srv_heap->Heap(), 1, true, "TextureContext SRV Heap");

    if (format) {
        const auto desc = texture->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = (DXGI_FORMAT)*format;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize > 1) {
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray.MipLevels = 1;
            srv_desc.Texture2DArray.MostDetailedMip = 0;
            srv_desc.Texture2DArray.FirstArraySlice = 0;
            srv_desc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
            srv_desc.Texture2DArray.PlaneSlice = 0;
            srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        } else {
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.PlaneSlice = 0;
            srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
        }
        device->CreateShaderResourceView(texture.Get(), &srv_desc, get_srv_cpu());
    } else {
        device->CreateShaderResourceView(texture.Get(), nullptr, get_srv_cpu());
    }

    return true;
}
}
