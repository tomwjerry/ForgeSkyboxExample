#define MAX_PLANETS 20 // Does not affect test, just for allocating space in uniform block. Must match with shader.

// Interfaces
#include "TheForge/Common_3/Application/Interfaces/IApp.h"
#include "TheForge/Common_3/Application/Interfaces/ICameraController.h"
#include "TheForge/Common_3/Application/Interfaces/IFont.h"
#include "TheForge/Common_3/Application/Interfaces/IProfiler.h"
#include "TheForge/Common_3/Application/Interfaces/IScreenshot.h"
#include "TheForge/Common_3/Application/Interfaces/IUI.h"
#include "TheForge/Common_3/Game/Interfaces/IScripting.h"
#include "TheForge/Common_3/Utilities/Interfaces/IFileSystem.h"
#include "TheForge/Common_3/Utilities/Interfaces/ILog.h"
#include "TheForge/Common_3/Utilities/Interfaces/ITime.h"

#include "TheForge/Common_3/Utilities/RingBuffer.h"

// Renderer
#include "TheForge/Common_3/Graphics/Interfaces/IGraphics.h"
#include "TheForge/Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

// Math
#include "TheForge/Common_3/Utilities/Math/MathTypes.h"

#include "TheForge/Common_3/Utilities/Interfaces/IMemory.h"

struct PlanetInfoStruct
{
	mat4  mTranslationMat;
	mat4  mScaleMat;
	mat4  mSharedMat; // Matrix to pass down to children
	vec4  mColor;
	uint  mParentIndex;
	float mYOrbitSpeed; // Rotation speed around parent
	float mZOrbitSpeed;
	float mRotationSpeed; // Rotation speed around self
	float mMorphingSpeed; // Speed of morphing betwee cube and sphere
};

struct UniformBlock
{
	CameraMatrix mProjectView;
	mat4         mToWorldMat[MAX_PLANETS];
	vec4         mColor[MAX_PLANETS];
	float        mGeometryWeight[MAX_PLANETS][4];

	// Point Light Information
	vec4 mLightPosition;
	vec4 mLightColor;
};

struct UniformBlockSky
{
	CameraMatrix mProjectView;
};

class WBIGame: public IApp
{	
private:
	const char* appName = "WBIGame";
	Renderer* pRenderer = NULL;
	const uint32_t gDataBufferCount = 2;
	QueryPool* pPipelineStatsQueryPool[2] = {};
	Queue* pGraphicsQueue = NULL;
	GpuCmdRing gGraphicsCmdRing = {};
	Semaphore* pImageAcquiredSemaphore = NULL;
	uint32_t gFrameIndex = 0;

	UniformBlock     gUniformData;
	UniformBlockSky  gUniformDataSky;

	ICameraController* pCameraController = NULL;

	SwapChain* pSwapChain = NULL;
	RenderTarget* pDepthBuffer = NULL;

	const char* pSkyBoxImageFileNames[6] = {
		"Skybox_right1.tex", "Skybox_left2.tex", "Skybox_top3.tex",
		"Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex"
	};
	const float skyBoxPoints[144] = {
		10.0f, -10.0f, -10.0f, 6.0f, // -z
		-10.0f, -10.0f, -10.0f, 6.0f, -10.0f, 10.0f, -10.0f, 6.0f,
		-10.0f, 10.0f, -10.0f, 6.0f, 10.0f, 10.0f, -10.0f, 6.0f,
		10.0f, -10.0f, -10.0f, 6.0f,

		-10.0f, -10.0f, 10.0f, 2.0f, // -x
		-10.0f, -10.0f, -10.0f, 2.0f, -10.0f, 10.0f, -10.0f, 2.0f,
		-10.0f, 10.0f, -10.0f, 2.0f, -10.0f, 10.0f,  10.0f, 2.0f, 
		-10.0f, -10.0f, 10.0f, 2.0f,

		10.0f, -10.0f, -10.0f, 1.0f, // +x
		10.0f, -10.0f, 10.0f, 1.0f, 10.0f, 10.0f, 10.0f, 1.0f,
		10.0f, 10.0f, 10.0f, 1.0f, 10.0f, 10.0f, -10.0f, 1.0f,
		10.0f, -10.0f, -10.0f, 1.0f,

		-10.0f, -10.0f, 10.0f, 5.0f, // +z
		-10.0f, 10.0f, 10.0f, 5.0f, 10.0f, 10.0f, 10.0f, 5.0f,
		10.0f, 10.0f, 10.0f, 5.0f, 10.0f, -10.0f, 10.0f, 5.0f,
		-10.0f, -10.0f, 10.0f, 50.0f,

		-10.0f, 10.0f, -10.0f, 3.0f, // +y
		10.0f, 10.0f, -10.0f, 3.0f, 10.0f, 10.0f, 10.0f, 3.0f,
		10.0f, 10.0f, 10.0f, 3.0f, -10.0f, 10.0f, 10.0f, 3.0f,
		-10.0f, 10.0f, -10.0f, 3.0f,

		10.0f, -10.0f, 10.0f, 4.0f, // -y
		10.0f, -10.0f, -10.0f, 4.0f, -10.0f, -10.0f, -10.0f, 4.0f,
		-10.0f, -10.0f, -10.0f, 4.0f, -10.0f, -10.0f, 10.0f, 4.0f,
		10.0f, -10.0f, 10.0f, 40.0f
	};

	Shader* pSkyBoxDrawShader = NULL;
	Buffer* pSkyBoxVertexBuffer = NULL;
	Pipeline* pSkyBoxDrawPipeline = NULL;
	RootSignature* pRootSignature = NULL;
	Sampler* pSamplerSkyBox = NULL;
	Texture* pSkyBoxTextures[6];
	DescriptorSet* pDescriptorSetTexture = { NULL };
	DescriptorSet* pDescriptorSetUniforms = { NULL };

	Buffer* pProjViewUniformBuffer[2] = { NULL };
	Buffer* pSkyboxUniformBuffer[2] = { NULL };
public:
	const char* GetName()
	{
		return appName;
	}

	bool Init()
	{
		RendererDesc rendererSettings;
		memset(&rendererSettings, 0, sizeof(rendererSettings));
		initGPUConfiguration(rendererSettings.pExtendedSettings);
		initRenderer(appName, &rendererSettings, &pRenderer);
		if (!pRenderer)
		{
			ShowUnsupportedMessage("Failed to initialize renderer.");
			return false;
		}

		setupGPUConfigurationPlatformParameters(pRenderer, rendererSettings.pExtendedSettings);

		if (pRenderer->pGpu->mPipelineStatsQueries)
		{
			QueryPoolDesc poolDesc = {};
			poolDesc.mQueryCount = 3; // Multiview
			poolDesc.mType = QUERY_TYPE_PIPELINE_STATISTICS;
			for (uint32_t i = 0; i < gDataBufferCount; ++i)
			{
				initQueryPool(pRenderer, &poolDesc, &pPipelineStatsQueryPool[i]);
			}
		}

		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

		GpuCmdRingDesc cmdRingDesc = {};
		cmdRingDesc.pQueue = pGraphicsQueue;
		cmdRingDesc.mPoolCount = gDataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;
		initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);
		
		initSemaphore(pRenderer, &pImageAcquiredSemaphore);

		initResourceLoaderInterface(pRenderer);

		// Skybox textures
		for (int i = 0; i < 6; ++i)
		{
			TextureLoadDesc textureDesc = {};
			textureDesc.pFileName = pSkyBoxImageFileNames[i];
			textureDesc.ppTexture = &pSkyBoxTextures[i];
			textureDesc.mCreationFlag = TEXTURE_CREATION_FLAG_SRGB;
			addResource(&textureDesc, NULL);
		}

		// Sampler
		SamplerDesc samplerDesc = {
			FILTER_LINEAR,
			FILTER_LINEAR,
			MIPMAP_MODE_NEAREST,
			ADDRESS_MODE_CLAMP_TO_EDGE,
			ADDRESS_MODE_CLAMP_TO_EDGE,
			ADDRESS_MODE_CLAMP_TO_EDGE };
		addSampler(pRenderer, &samplerDesc, &pSamplerSkyBox);

		// More skybox
		uint64_t skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
		BufferLoadDesc skyBoxVbDesc = {};
		skyBoxVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		skyBoxVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		skyBoxVbDesc.mDesc.mSize = skyBoxDataSize;
		skyBoxVbDesc.pData = skyBoxPoints;
		skyBoxVbDesc.ppBuffer = &pSkyBoxVertexBuffer;
		addResource(&skyBoxVbDesc, NULL);

		// Buffers
		BufferLoadDesc ubDesc = {};
		ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubDesc.pData = NULL;

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			ubDesc.mDesc.pName = "ProjViewUniformBuffer";
			ubDesc.mDesc.mSize = sizeof(UniformBlock);
			ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
			addResource(&ubDesc, NULL);

			ubDesc.mDesc.pName = "SkyboxUniformBuffer";
			ubDesc.mDesc.mSize = sizeof(UniformBlockSky);
			ubDesc.ppBuffer = &pSkyboxUniformBuffer[i];
			addResource(&ubDesc, NULL);
		}

		UserInterfaceDesc uiRendererDesc = {};
		uiRendererDesc.pRenderer = pRenderer;
		initUserInterface(&uiRendererDesc);

		waitForAllResourceLoads();

		CameraMotionParameters cmp{ 160.0f, 600.0f, 200.0f };
		vec3 camPos = { 0.0f, 0.0f, -10.0f };
		vec3 lookAt = { 0.0f, 0.0f, 0.0f };
		pCameraController = initFpsCameraController(camPos, lookAt);

		AddCustomInputBindings();

		gFrameIndex = 0;

		return true;
	}

	void Exit()
	{
		exitCameraController(pCameraController);

		exitUserInterface();

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			removeResource(pProjViewUniformBuffer[i]);
			removeResource(pSkyboxUniformBuffer[i]);
			if (pRenderer->pGpu->mPipelineStatsQueries)
			{
				exitQueryPool(pRenderer, pPipelineStatsQueryPool[i]);
			}
		}

		removeResource(pSkyBoxVertexBuffer);

		for (uint i = 0; i < 6; ++i)
		{
			removeResource(pSkyBoxTextures[i]);
		}

		removeSampler(pRenderer, pSamplerSkyBox);

		exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
		exitSemaphore(pRenderer, pImageAcquiredSemaphore);

		exitResourceLoaderInterface(pRenderer);

		exitQueue(pRenderer, pGraphicsQueue);

		exitRenderer(pRenderer);
		exitGPUConfiguration();
		pRenderer = NULL;
	}

	bool Load(ReloadDesc* pReloadDesc)
	{
		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			addShaders();
			addRootSignatures();
			addDescriptorSets();
		}

		// If we resize window
		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			// UI Stuff

			if (!addSwapChain())
			{
				return false;
			}

			if (!addDepthBuffer())
			{
				return false;
			}
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			addPipelines();
		}

		prepareDescriptorSets();

		// UI

		// Font

		return true;
	}

	void Unload(ReloadDesc* pReloadDesc)
	{
		waitQueueIdle(pGraphicsQueue);

		// Font

		// UI

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			removePipelines();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			removeSwapChain(pRenderer, pSwapChain);
			removeRenderTarget(pRenderer, pDepthBuffer);
		}

		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			removeDescriptorSets();
			removeRootSignatures();
			removeShaders();
		}
	}

	void Update(float deltaTime)
	{
		if (!uiIsFocused())
		{
			pCameraController->onMove({
				inputGetValue(0, CUSTOM_MOVE_X),
				inputGetValue(0, CUSTOM_MOVE_Y)
			});
			pCameraController->onRotate({
				inputGetValue(0, CUSTOM_LOOK_X),
				inputGetValue(0, CUSTOM_LOOK_Y)
			});
			pCameraController->onMoveY(inputGetValue(0, CUSTOM_MOVE_UP));

			if (inputGetValue(0, CUSTOM_RESET_VIEW))
			{
				pCameraController->resetView();
			}

			if (inputGetValue(0, CUSTOM_TOGGLE_UI))
			{
				uiToggleActive();
			}

			if (inputGetValue(0, CUSTOM_EXIT))
			{
				requestShutdown();
			}
		}

		pCameraController->update(deltaTime);

		static float currentTime = 0.0f;
		currentTime += deltaTime * 1000.0f;

		mat4 viewMat = pCameraController->getViewMatrix();

		const float aspectInverse = (float)mSettings.mWidth / (float)mSettings.mHeight;
		const float horizontalFov = PI / 2.0f;
		CameraMatrix projMat = CameraMatrix::perspective(horizontalFov, aspectInverse, 0.1f, 1000.0f);
		gUniformData.mProjectView = projMat * viewMat;

		gUniformData.mLightPosition = vec4(0.0f, 0.0f, 0.0f, 0.0f);
		gUniformData.mLightColor = vec4(0.9f, 0.9f, 0.7f, 1.0f);

		viewMat.setTranslation(vec3(0.0f, 0.0f, 0.0f));
		gUniformDataSky = {};
		gUniformDataSky.mProjectView = projMat * viewMat;
	}

	void Draw()
	{
		if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
		{
			waitQueueIdle(pGraphicsQueue);
			::toggleVSync(pRenderer, &pSwapChain);
		}

		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer,
			pSwapChain,
			pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

		// Stall if CPU is running "gDataBufferCount" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			waitForFences(pRenderer, 1, &elem.pFence);
		}

		// Update uniform buffers
		BufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[gFrameIndex] };
		
		beginUpdateResource(&viewProjCbv);
		if (viewProjCbv.pMappedData != nullptr)
		{
			memcpy(viewProjCbv.pMappedData, &gUniformData, sizeof(gUniformData));
		}
		endUpdateResource(&viewProjCbv);

		BufferUpdateDesc skyboxViewProjCbv = { pSkyboxUniformBuffer[gFrameIndex] };
		beginUpdateResource(&skyboxViewProjCbv);
		if (skyboxViewProjCbv.pMappedData != nullptr)
		{
			memcpy(skyboxViewProjCbv.pMappedData, &gUniformDataSky, sizeof(gUniformDataSky));
		}
		endUpdateResource(&skyboxViewProjCbv);

		// Reset cmd pool for this frame
		resetCmdPool(pRenderer, elem.pCmdPool);

		Cmd* cmd = elem.pCmds[0];
		beginCmd(cmd);

		// What does this do?
		if (pRenderer->pGpu->mPipelineStatsQueries)
		{
			cmdResetQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], 0, 2);
			QueryDesc queryDesc = { 0 };
			cmdBeginQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
		}

		RenderTargetBarrier barriers[] = {
			{ pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
		};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		// Clean screen, why so much code?
		BindRenderTargetsDesc bindRenderTargets = {};
		bindRenderTargets.mRenderTargetCount = 1;
		bindRenderTargets.mRenderTargets[0] = { pRenderTarget, LOAD_ACTION_CLEAR };
		bindRenderTargets.mDepthStencil = { pDepthBuffer, LOAD_ACTION_CLEAR };
		cmdBindRenderTargets(cmd, &bindRenderTargets);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

		// Skybox
		const uint32_t skyboxVbStride = sizeof(float) * 4;
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
		cmdBindPipeline(cmd, pSkyBoxDrawPipeline);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSetTexture);
		cmdBindDescriptorSet(cmd, gFrameIndex * 2 + 0, pDescriptorSetUniforms);
		cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer, &skyboxVbStride, NULL);
		cmdDraw(cmd, 36, 0);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);

		cmdBindRenderTargets(cmd, NULL);

		// What does this do??
		if (pRenderer->pGpu->mPipelineStatsQueries)
		{
			QueryDesc queryDesc = { 0 };
			cmdEndQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);

			// I guess we change layer here or something
			queryDesc = { 1 };
			cmdBeginQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
		}

		// UI...

		cmdBindRenderTargets(cmd, NULL);

		// Do barriers have anything to do with render layers?
		barriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		// Again this stuff
		if (pRenderer->pGpu->mPipelineStatsQueries)
		{
			QueryDesc queryDesc = { 1 };
			cmdEndQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
			cmdResolveQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], 0, 2);
		}

		endCmd(cmd);

		// At this point I dont have remotely any idea what is going on
		FlushResourceUpdateDesc flushUpdateDesc = {};
		flushUpdateDesc.mNodeIndex = 0;
		flushResourceUpdates(&flushUpdateDesc);
		Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore };

		// Submit something, I dont know
		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &elem.pSemaphore;
		submitDesc.ppWaitSemaphores = waitSemaphores;
		submitDesc.pSignalFence = elem.pFence;
		queueSubmit(pGraphicsQueue, &submitDesc);

		// Swap chain, what, is this double buffer or what?
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = (uint8_t)swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.ppWaitSemaphores = &elem.pSemaphore;
		presentDesc.mSubmitDone = true;

		queuePresent(pGraphicsQueue, &presentDesc);

		gFrameIndex = (gFrameIndex + 1) % gDataBufferCount;
	}

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
		swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, COLOR_SPACE_SDR_SRGB);
		swapChainDesc.mColorSpace = COLOR_SPACE_SDR_SRGB;
		swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
		swapChainDesc.mFlags = SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	bool addDepthBuffer()
	{
		RenderTargetDesc depthRT = {};
		depthRT.mArraySize = 1;
		depthRT.mClearValue.depth = 0.0f;
		depthRT.mClearValue.stencil = 0;
		depthRT.mDepth = 1;
		depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
		depthRT.mStartState = RESOURCE_STATE_DEPTH_WRITE;
		depthRT.mHeight = mSettings.mHeight;
		depthRT.mSampleCount = SAMPLE_COUNT_1;
		depthRT.mSampleQuality = 0;
		depthRT.mWidth = mSettings.mWidth;
		depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE | TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
		addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

		return pDepthBuffer != NULL;
	}

	void addDescriptorSets()
	{
		DescriptorSetDesc desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetTexture);
		desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gDataBufferCount * 2 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetUniforms);
	}

	void removeDescriptorSets()
	{
		removeDescriptorSet(pRenderer, pDescriptorSetTexture);
		removeDescriptorSet(pRenderer, pDescriptorSetUniforms);
	}

	void addRootSignatures()
	{
		Shader* shaders[1] = {
			pSkyBoxDrawShader
		};


		RootSignatureDesc rootDesc = {};
		rootDesc.mShaderCount = 1;
		rootDesc.ppShaders = shaders;
		addRootSignature(pRenderer, &rootDesc, &pRootSignature);
	}

	void removeRootSignatures()
	{
		removeRootSignature(pRenderer, pRootSignature);
	}

	void addShaders()
	{
		ShaderLoadDesc skyShader = {};
		skyShader.mVert.pFileName = "skybox.vert";
		skyShader.mFrag.pFileName = "skybox.frag";
		addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);
	}

	void removeShaders()
	{
		removeShader(pRenderer, pSkyBoxDrawShader);
	}

	void addPipelines()
	{
		// Generic rasterizer state
		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;

		// We will need this later
		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = CMP_GEQUAL;

		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.mVRFoveatedRendering = true;

		// Skybox pipeline
		VertexLayout vertexLayout = {};
		vertexLayout.mBindingCount = 1;
		vertexLayout.mBindings[0].mStride = sizeof(float4);
		vertexLayout.mAttribCount = 1;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		pipelineSettings.pVertexLayout = &vertexLayout;

		pipelineSettings.pDepthState = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pShaderProgram = pSkyBoxDrawShader; //-V519
		addPipeline(pRenderer, &desc, &pSkyBoxDrawPipeline);
	}

	void removePipelines()
	{
		removePipeline(pRenderer, pSkyBoxDrawPipeline);
	}

	void prepareDescriptorSets()
	{
		DescriptorData params[7] = {};
		params[0].pName = "RightText";
		params[0].ppTextures = &pSkyBoxTextures[0];
		params[1].pName = "LeftText";
		params[1].ppTextures = &pSkyBoxTextures[1];
		params[2].pName = "TopText";
		params[2].ppTextures = &pSkyBoxTextures[2];
		params[3].pName = "BotText";
		params[3].ppTextures = &pSkyBoxTextures[3];
		params[4].pName = "FrontText";
		params[4].ppTextures = &pSkyBoxTextures[4];
		params[5].pName = "BackText";
		params[5].ppTextures = &pSkyBoxTextures[5];
		params[6].pName = "uSampler0";
		params[6].ppSamplers = &pSamplerSkyBox;
		updateDescriptorSet(pRenderer, 0, pDescriptorSetTexture, 7, params);

		for (uint32_t i = 0; i < gDataBufferCount; ++i)
		{
			DescriptorData uParams[1] = {};
			uParams[0].pName = "uniformBlock";
			uParams[0].ppBuffers = &pSkyboxUniformBuffer[i];
			updateDescriptorSet(pRenderer, i * 2 + 0, pDescriptorSetUniforms, 1, uParams);

			uParams[0].pName = "uniformBlock";
			uParams[0].ppBuffers = &pProjViewUniformBuffer[i];
			updateDescriptorSet(pRenderer, i * 2 + 1, pDescriptorSetUniforms, 1, uParams);
		}
	}
};
DEFINE_APPLICATION_MAIN(WBIGame)
