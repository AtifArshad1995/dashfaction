#pragma once

#include <unordered_map>
#include <d3d11.h>
#include <patch_common/ComPtr.h>

namespace df::gr::d3d11
{
    class StateManager
    {
    public:
        StateManager(ComPtr<ID3D11Device> device);
        ID3D11RasterizerState* lookup_rasterizer_state(D3D11_CULL_MODE cull_mode);
        ID3D11SamplerState* lookup_sampler_state(rf::gr::Mode mode, int slot);
        ID3D11BlendState* lookup_blend_state(rf::gr::Mode mode);
        ID3D11DepthStencilState* lookup_depth_stencil_state(rf::gr::Mode mode);

    private:
        ComPtr<ID3D11Device> device_;
        std::unordered_map<int, ComPtr<ID3D11SamplerState>> sampler_state_cache_;
        std::unordered_map<int, ComPtr<ID3D11BlendState>> blend_state_cache_;
        std::unordered_map<int, ComPtr<ID3D11DepthStencilState>> depth_stencil_state_cache_;
        std::unordered_map<D3D11_CULL_MODE, ComPtr<ID3D11RasterizerState>> rasterizer_state_cache_;
    };
}
