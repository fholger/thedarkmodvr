/*****************************************************************************
                    The Dark Mod GPL Source Code

 This file is part of the The Dark Mod Source Code, originally based
 on the Doom 3 GPL Source Code as published in 2011.

 The Dark Mod Source Code is free software: you can redistribute it
 and/or modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation, either version 3 of the License,
 or (at your option) any later version. For details, see LICENSE.TXT.

 Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/
#include "precompiled.h"
#include "D3D11Helper.h"

#ifdef WIN32
#include <dxgi1_4.h>
#include "../../hlprogs/VS_Flip.h"
#include "../../hlprogs/PS_Flip.h"

void D3D11Helper::Init( XrGraphicsRequirementsD3D11KHR &reqs ) {
	device.Reset();

	IDXGIFactory4 *dxgiFactory;
	if ( FAILED ( CreateDXGIFactory1( __uuidof(IDXGIFactory4), (void**)&dxgiFactory ) ) ) {
		common->Error( "Failed to create DXGI factory" );
	}
	IDXGIAdapter *adapter;
	if ( FAILED ( dxgiFactory->EnumAdapterByLuid( reqs.adapterLuid, __uuidof(IDXGIAdapter), (void**)&adapter ) ) ) {
		common->Error( "Failed to find DXGI adapter" );
	}

	HRESULT result = D3D11CreateDevice( adapter,
			D3D_DRIVER_TYPE_UNKNOWN, 
			nullptr,
			0,
			&reqs.minFeatureLevel,
			1,
			D3D11_SDK_VERSION,
			device.GetAddressOf(),
			nullptr,
			context.GetAddressOf() );
	if ( FAILED( result ) ) {
		common->Error( "Failed to create D3D11 device: %08x", (unsigned)result );
	}

	glDeviceHandle = qwglDXOpenDeviceNV(device.Get());
	if ( glDeviceHandle == nullptr ) {
		common->Error( "Failed to open GL interop to DX device" );
	}

	InitFlipShader();
}

void D3D11Helper::Shutdown() {
	qwglDXCloseDeviceNV( glDeviceHandle );
	context.Reset();
	device.Reset();
}

XrGraphicsBindingD3D11KHR D3D11Helper::CreateGraphicsBinding() {
	ID3D11Device *devicePtr = device.Get();
	return {
		XR_TYPE_GRAPHICS_BINDING_D3D11_KHR,
		nullptr,
		devicePtr,
	};	
}

void D3D11Helper::RenderTextureFlipped( ID3D11ShaderResourceView *sourceTexture, ID3D11RenderTargetView *target, int width, int height ) {
	context->OMSetRenderTargets( 1, &target, nullptr );
	context->OMSetBlendState( nullptr, nullptr, 0xffffffff );
	context->OMSetDepthStencilState( nullptr, 0 );
	context->VSSetShader( flipVertexShader.Get(), nullptr, 0 );
	context->PSSetShader( flipPixelShader.Get(), nullptr, 0 );
	context->PSSetShaderResources( 0, 1, &sourceTexture );
	context->PSSetSamplers( 0, 1, sampler.GetAddressOf() );
	context->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
	context->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
	context->IASetInputLayout( nullptr );
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	D3D11_VIEWPORT vp;
	vp.TopLeftX = vp.TopLeftY = 0;
	vp.Width = width;
	vp.Height = height;
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	context->RSSetViewports( 1, &vp );
	context->RSSetState( state.Get() );

	context->Draw( 3, 0 );

	context->OMSetRenderTargets( 0, nullptr, nullptr );
	context->PSSetShaderResources( 0, 0, nullptr );
}

void D3D11Helper::InitFlipShader() {
	if ( FAILED( device->CreateVertexShader( VS_Flip, sizeof(VS_Flip), nullptr, flipVertexShader.GetAddressOf() ) ) ) {
		common->Error( "Could not load flip vertex shader" );
	}
	if ( FAILED( device->CreatePixelShader( PS_Flip, sizeof(PS_Flip), nullptr, flipPixelShader.GetAddressOf() ) ) ) {
		common->Error( "Could not load flip pixel shader" );
	}

	D3D11_SAMPLER_DESC sd;
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MipLODBias = 0;
	sd.MaxAnisotropy = 1;
	sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sd.MinLOD = 0;
	sd.MaxLOD = 0;
	if ( FAILED( device->CreateSamplerState( &sd, sampler.GetAddressOf() ) ) ) {
		common->Error( "Could not create sampler state" );
	}

	D3D11_RASTERIZER_DESC rd;
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.FrontCounterClockwise = TRUE;
	rd.DepthBias = 0;
	rd.DepthBiasClamp = 0;
	rd.SlopeScaledDepthBias = 0;
	rd.DepthClipEnable = FALSE;
	rd.ScissorEnable = FALSE;
	rd.MultisampleEnable = FALSE;
	rd.AntialiasedLineEnable = FALSE;
	if ( FAILED( device->CreateRasterizerState( &rd, state.GetAddressOf() ) ) ) {
		common->Error( "Failed to create rasterizer state" );
	}
}

#endif
