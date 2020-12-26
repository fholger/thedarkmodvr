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
#pragma once

#ifdef WIN32
#include <d3d11.h>
#include <wrl/client.h>
#include "xr_loader.h"

class D3D11Helper {
public:
	void Init( XrGraphicsRequirementsD3D11KHR &reqs );
	void Shutdown();

	XrGraphicsBindingD3D11KHR CreateGraphicsBinding();

	void RenderTextureFlipped( ID3D11ShaderResourceView *sourceTexture, ID3D11RenderTargetView *target, int width, int height );
	
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	ComPtr<ID3D11Device> device;
	HANDLE glDeviceHandle = nullptr;

private:
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11VertexShader> flipVertexShader;
	ComPtr<ID3D11PixelShader> flipPixelShader;
	ComPtr<ID3D11SamplerState> sampler;
	ComPtr<ID3D11RasterizerState> state;

	void InitFlipShader();
};

#endif
