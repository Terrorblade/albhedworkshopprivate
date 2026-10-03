struct PVec2f { float x, y; };
struct PVec4f { float x, y, z, w; };
struct PMat4x3 { float m[12]; };
struct PMat4 { float m[16]; };

struct D3D11_DEPTH_STENCILOP_DESC { unsigned __int8 _opaque[16]; };
struct PAnimationEventList { unsigned __int8 _opaque[8]; };
struct PAnimationSet { unsigned __int8 _opaque[24]; };
struct PAnimationWeightedBlenderController { unsigned __int8 _opaque[28]; };
struct PClassDescriptor { unsigned __int8 _opaque[148]; };
struct PConstantBuffer { unsigned __int8 _opaque[60]; };
struct PDataBlock { unsigned __int8 _opaque[64]; };
struct PIndexDataBlock { unsigned __int8 _opaque[60]; };
struct PIndirectArgsBuffer { unsigned __int8 _opaque[24]; };
struct PInputMap { unsigned __int8 _opaque[8]; };
struct PMaterialSet { unsigned __int8 _opaque[8]; };
struct PMeshSegment { unsigned __int8 _opaque[108]; };
struct PPhysicsRigidBody { unsigned __int8 _opaque[228]; };
struct PPhysicsWorld { unsigned __int8 _opaque[168]; };
struct PSamplerState { unsigned __int8 _opaque[36]; };
struct PShaderComputeProgram { unsigned __int8 _opaque[1252]; };
struct PShaderFragmentProgram { unsigned __int8 _opaque[1252]; };
struct PShaderGeometryProgram { unsigned __int8 _opaque[1252]; };
struct PShaderPassParameterLocationTypesConstantBuffer { unsigned __int8 _opaque[24]; };
struct PShaderVertexProgram { unsigned __int8 _opaque[1264]; };
struct PStreamInputLayoutD3D11 { unsigned __int8 _opaque[12]; };
struct PString { unsigned __int8 _opaque[4]; };
struct PStructuredBuffer { unsigned __int8 _opaque[72]; };
struct PTexture2D { unsigned __int8 _opaque[116]; };
struct PTexture3D { unsigned __int8 _opaque[104]; };
struct PTextureCubeMap { unsigned __int8 _opaque[104]; };
struct PTimeController { unsigned __int8 _opaque[8]; };

struct CD3D11_BLEND_DESC;
struct CD3D11_DEPTH_STENCIL_DESC;
struct CD3D11_RASTERIZER_DESC;
struct D3D11_RENDER_TARGET_BLEND_DESC;
struct PAnimationNetworkInstance;
struct PAnimationTargetBlenderController;
struct PAnimatableComponent;
struct PAnimationChannelTimes;
struct PAnimationChannel;
struct PAnimationChannelBase;
struct PAnimationChannelTarget;
struct PAnimationChannelTarget_4_;
struct PAnimationChannel___4_;
struct PAnimationClipBinding;
struct PAnimationClip;
struct PAnimationClipBindingChannelMap;
struct PAnimationClipBindingDataBlockCache;
struct PAnimationClip___;
struct PAnimationConstantChannel_4_;
struct PAnimationController;
struct PAnimationDataSource;
struct PAnimationDataSourceListEntry;
struct PAnimationDataSourceListEntry_4_;
struct PAnimationDataSource___4_;
struct PAnimationEvent;
struct PAnimationEventController;
struct PAnimationEvent_4_;
struct PAnimationNetworkInstanceTarget_4_;
struct PAnimationSlotArray_4_;
struct PAnimationSlotFilter;
struct PAnimationSlotFilterDeferredLoad;
struct PAnimationSlotFilterDeferredLoad_4_;
struct PAnimationSlotListIndex;
struct PAnimationSlotListIndex_4_;
struct PAnimationSpuTargetBlenderController;
struct PArray_float_4_;
struct PArray_int_4_;
struct PArray_unsigned_char_1_;
struct PArray_unsigned_char_4_;
struct PArray_unsigned_char_const___4_;
struct PArray_unsigned_int_4_;
struct PAssetReference;
struct PAssetReferenceImport;
struct PTypedObject;
struct PAttachableComponent;
struct PBase_const___4_;
struct PBitmapFont;
struct PBitmapFontCharInfo;
struct PBitmapFontCharInfo_4_;
struct PTimeIntervalController;
struct PTimeScaleOffsetController;
struct PBlendableAnimationSource;
struct PBlendableAnimationSource_4_;
struct PCamera;
struct PPhysicsCharacterCamera;
struct PCameraControllerComponent;
struct PCameraOrthographic;
struct PCameraPerspective;
struct PCameraProjection;
struct PClassCallableMethodScript;
struct PClassDataMemberDynamic;
struct PClassDescriptorDynamic;
struct PClassMember;
struct PClusterHeaderBase;
struct PClusterHeaderD3D11;
struct PComponent;
struct PConstantBufferBase;
struct PContextSwitch_const___4_;
struct PContextVariantFoldingTable;
struct PContextVariantFoldingTable_4_;
struct PDataBlockBase;
struct PDataBlockBufferD3D11_;
struct PDataBlockD3D11;
struct PDataBlockD3D11_4_;
struct PDeferredLightingBase;
struct PDepthOfFieldBase;
struct PDynamicDataBlock;
struct PDynamicMesh;
struct PDynamicMeshInstance;
struct PDynamicSegmentDesc;
struct PDynamicSegmentDesc_4_;
struct PEffect;
struct PEffectVariant;
struct PEffectVariant___4_;
struct PEntity;
struct PGameSettings;
struct PGlowBase;
struct PIndexDataBlockBase;
struct PIndexDataBlockBufferD3D11_;
struct PIndexDataBlockD3D11;
struct PInputAction;
struct PInputAction___4_;
struct PInputMap___4_;
struct PInputMapper;
struct PInputSource;
struct PInputSourceJoypadAxis;
struct PInputSourceJoypadButton;
struct PInputSourceKey;
struct PInputSourceMouseButton;
struct PInputSourceMouseDeltaX;
struct PInputSourceMouseDeltaY;
struct PInputSource___4_;
struct PInstanceListHeader;
struct PLODGroup;
struct PLODLevel;
struct PLODLevel_4_;
struct PLight;
struct PLightType_const___4_;
struct PLocator;
struct PMaterial;
struct PMaterialSwitch;
struct PMaterialSwitch_4_;
struct PMaterial___;
struct PMatrix4_4_;
struct PMesh;
struct PMeshInstance;
struct PMeshInstanceAttachPoint;
struct PMeshInstanceBounds;
struct PMeshInstanceSegmentContext_4_;
struct PMeshInstanceSegmentStreamBinding;
struct PMeshInstanceSegmentStreamBinding_const___;
struct PMeshInstance___;
struct PMeshSegmentBase;
struct PMeshSegmentD3D11;
struct PMeshSegment_4_;
struct PModifierAndInputs;
struct PModifierAndInputs_4_;
struct PModifierNetwork;
struct PModifierNetworkBuffer;
struct PModifierNetworkBuffer_4_;
struct PModifierNetworkDynamicMeshSegment;
struct PModifierNetworkDynamicMeshSegment_4_;
struct PModifierNetworkInfoPacket;
struct PModifierNetworkInfoPacket_Buffer;
struct PModifierNetworkInfoPacket_ModifierCode;
struct PModifierNetworkInfoPacket_ModifierInstance;
struct PModifierNetworkInstance;
struct PRenderStream;
struct PModifierNetworkInstanceInput;
struct PModifierNetworkInstancePacketInput;
struct PModifierNetworkInstance___4_;
struct PMotionBlurBase;
struct PNameComponent;
struct PNode;
struct PNodeContext_4_;
struct POccluderGeometryInstance;
struct POccluderGeometryObject;
struct PParameterBuffer;
struct PParameterBufferBase;
struct PPhysicsBoxBase;
struct PPhysicsCallbackData;
struct PPhysicsCapsuleBase;
struct PScriptCallbackHandler;
struct PPhysicsCharacterControllerBase;
struct PPhysicsCylinderBase;
struct PPhysicsMaterial;
struct PPhysicsMeshBase;
struct PPhysicsModel;
struct PPhysicsPlaneBase;
struct PPhysicsRigidBodyBase;
struct PPhysicsShapeBase;
struct PPhysicsShape___;
struct PPhysicsSphereBase;
struct PPhysicsTaperedCapsule;
struct PPhysicsTaperedCylinder;
struct PPhysicsWorldBase;
struct PPostEffectBase;
struct PPostEffectBase___4_;
struct PQWord_16_;
struct PWorldMatrix;
struct PQuarryComponent;
struct PRaycastResult;
struct PRenderStreamInput;
struct PRenderStreamInput_4_;
struct PRenderStream_4_;
struct PRenderTargetBase;
struct PSamplerStateBase;
struct PSceneRenderPass;
struct PSceneRenderPass_4_;
struct PSceneRenderPass___4_;
struct PScreenSpaceReflectionBase;
struct PScript;
struct PScriptableComponent;
struct PScriptedComponent;
struct PShader;
struct PShaderParameterCaptureBufferByteAddressBuffer;
struct PShaderParameterCaptureBufferDataBlock;
struct PShaderParameterCaptureBufferIndexDataBlock;
struct PShaderParameterCaptureBufferLocation;
struct PShaderParameterCaptureBufferLocationSize;
struct PShaderParameterCaptureBufferLocationType;
struct PShaderParameterCaptureBufferLocationTypeConstantBuffer;
struct PShaderParameterCaptureBufferLocationTypeConstantBuffer_4_;
struct PShaderParameterCaptureBufferLocationType_2_;
struct PShaderParameterCaptureBufferLocation_2_;
struct PShaderParameterCaptureBufferRWByteAddressBuffer;
struct PShaderParameterCaptureBufferRWStructuredBuffer;
struct PShaderParameterCaptureBufferRWTexture2D;
struct PShaderParameterCaptureBufferRWTexture3D;
struct PShaderParameterCaptureBufferSampler;
struct PShaderParameterCaptureBufferStructuredBuffer;
struct PShaderParameterCaptureBufferTexture2D;
struct PShaderParameterCaptureBufferTexture3D;
struct PShaderParameterCaptureBufferTextureBase;
struct PShaderParameterCaptureBufferTextureCubeMap;
struct PShaderParameterCaptureConstantBuffer;
struct PShaderParameterDefinition;
struct PShaderParameterDefinition_4_;
struct PShaderPassBase;
struct PShaderPassStateD3D11;
struct PShaderPassD3D11;
struct PShaderPassInfo;
struct PShaderPassInfo_4_;
struct PShaderPassStateBase;
struct PShaderPass_4_;
struct PShaderProgramD3D11;
struct PShaderSource;
struct PShaderStreamDefinition;
struct PShaderStreamDefinition_4_;
struct PShaderVertexProgramD3D11;
struct PShader_4_;
struct PShadowCaster;
struct PShadowCasterType_const___4_;
struct PShadowSplit;
struct PShadowSplit_4_;
struct PShape;
struct PSharray_unsigned_int_;
struct PSkeletonJointBounds;
struct PSkeletonJointBounds_4_;
struct PSkinBoneRemap;
struct PSkinBoneRemap_2_;
struct PSpline;
struct PSplineFollowerComponent;
struct PSpriteAnimationInfo;
struct PSpriteAnimationInfoChar;
struct PSpriteAnimationInfoInstance;
struct PSpriteAttributes;
struct PSpriteCollection;
struct PStreamInputDescD3D11;
struct PStreamInputDescD3D11_4_;
struct PString_;
struct PString_4_;
struct PStructuredBufferBase;
struct PSubTextureInfo;
struct PSubTextureInfo_4_;
struct PTexture2DBase;
struct PTexture3DBase;
struct PTextureAtlasInfo;
struct PTextureCommonBase;
struct PTextureCubeMapBase;
struct PTimerComponent;
struct PTrigger;
struct PTriggerReceiverComponent;
struct PTriggerReceiverComponent___;
struct PTriggerReceiverTypeCallbackData;
struct PTypedObject_4_;
struct PVertexStream;
struct PVertexStream_4_;
struct Vector3_4_;

// CD3D11_BLEND_DESC  size=264  descriptor=0xcb47e8
struct CD3D11_BLEND_DESC {
  int AlphaToCoverageEnable;
  int IndependentBlendEnable;
  unsigned __int8 _pad_8[256];
};

// CD3D11_DEPTH_STENCIL_DESC  size=52  descriptor=0xcb43c8
struct CD3D11_DEPTH_STENCIL_DESC {
  int DepthEnable;
  unsigned __int8 _pad_4[8];
  int StencilEnable;
  unsigned __int8 StencilReadMask;
  unsigned __int8 StencilWriteMask;
  unsigned __int8 _pad_12[2];
  D3D11_DEPTH_STENCILOP_DESC FrontFace;
  D3D11_DEPTH_STENCILOP_DESC BackFace;
};

// CD3D11_RASTERIZER_DESC  size=40  descriptor=0xcb4008
struct CD3D11_RASTERIZER_DESC {
  unsigned __int8 _pad_0[8];
  int FrontCounterClockwise;
  int DepthBias;
  float DepthBiasClamp;
  float SlopeScaledDepthBias;
  int DepthClipEnable;
  int ScissorEnable;
  int MultisampleEnable;
  int AntialiasedLineEnable;
};

// D3D11_RENDER_TARGET_BLEND_DESC  size=32  descriptor=0xcb45d0
struct D3D11_RENDER_TARGET_BLEND_DESC {
  int BlendEnable;
  unsigned __int8 _pad_4[24];
  unsigned __int8 RenderTargetWriteMask;
  unsigned __int8 _pad_1d[3];
};

// Phyre::PAnimation::PAnimationNetworkInstance  size=60  descriptor=0xcace48
struct PAnimationNetworkInstance {
  unsigned __int8 _pad_0[24];
  PAnimationTargetBlenderController * m_targetBlender;
  unsigned int m_slotArrayElementsRequired;
  unsigned __int8 _pad_20[8];
  unsigned int m_preprocessBufferSize;
  unsigned __int8 _pad_2c[16];
};

// Phyre::PAnimation::PAnimationTargetBlenderController  size=16  descriptor=0xcacc00
struct PAnimationTargetBlenderController {
  unsigned __int8 _pad_0[12];
  PAnimationDataSource * m_animDataSource;
};

// Phyre::PAnimation::PAnimatableComponent  size=132  descriptor=0xcada40
struct PAnimatableComponent {
  unsigned __int8 _pad_0[16];
  unsigned __int8 m_animationSet[12];  // Phyre::PAnimation::PAnimationSet
  PAnimationWeightedBlenderController m_animationWeightedBlenderController;
  PAnimationTargetBlenderController m_animationTargetBlenderController;
  PAnimationNetworkInstance m_animationNetworkInstance;
};

// Phyre::PAnimation::PAnimationChannelTimes  size=12  descriptor=0xca99b8
struct PAnimationChannelTimes {
  unsigned int m_keyCount;
  unsigned __int8 _pad_4[8];
};

// Phyre::PAnimation::PAnimationChannel  size=52  descriptor=0xcaa0b8
struct PAnimationChannel {
  unsigned __int8 _pad_0[36];
  PAnimationChannelTimes m_times;
  unsigned int m_keyCount;
};

// Phyre::PAnimation::PAnimationChannelBase  size=36  descriptor=0xca9f30
struct PAnimationChannelBase {
  unsigned __int8 _pad_0[28];
  unsigned __int8 m_keyType[8];  // PAnimationKeyDataType
};

// Phyre::PAnimation::PAnimationChannelTarget  size=28  descriptor=0xca9c28
struct PAnimationChannelTarget {
  PClassDescriptor * m_instanceObjectType;
  void * m_instanceObject;
  PClassDescriptor * m_baseObjectType;
  unsigned __int8 m_baseObject[8];  // ?
  PString m_name;
  unsigned int m_index;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationChannelTarget,4>  size=None  descriptor=0xcab178
struct PAnimationChannelTarget_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationChannel *,4>  size=None  descriptor=0xcaac48
struct PAnimationChannel___4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationClipBinding  size=8  descriptor=0xcaa558
struct PAnimationClipBinding {
  unsigned __int16 m_spuBindingSize;
  unsigned __int16 m_channelCount;
  unsigned __int16 m_constantChannelCount;
  unsigned __int16 m_interpCountLength;
};

// Phyre::PAnimation::PAnimationClip  size=32  descriptor=0xcaa7e8
struct PAnimationClip {
  PAnimationClipBinding m_binding;
  unsigned __int8 _pad_8[12];
  float m_constantChannelStartTime;
  float m_constantChannelEndTime;
  PString m_name;
};

// Phyre::PAnimation::PAnimationClipBinding::PAnimationClipBindingChannelMap  size=4  descriptor=0xcaa428
struct PAnimationClipBindingChannelMap {
  __int16 m_destSlotArrayIndex;
  unsigned __int16 m_interp;
};

// Phyre::PAnimation::PAnimationClipBinding::PAnimationClipBindingDataBlockCache  size=16  descriptor=0xcaa4c0
struct PAnimationClipBindingDataBlockCache {
  unsigned __int8 _pad_0[8];
  unsigned int m_valueWidthAndKeyCount;
  unsigned __int16 m_sourceChannelIndex;
  unsigned __int8 _pad_e[2];
};

// Phyre::PSharray<Phyre::PAnimation::PAnimationClip *>  size=None  descriptor=0xcab298
struct PAnimationClip___ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationConstantChannel,4>  size=None  descriptor=0xcaab28
struct PAnimationConstantChannel_4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationController  size=20  descriptor=0xcabf68
struct PAnimationController {
  unsigned __int8 _pad_0[12];
  PAnimationClip * m_animationClip;
  PTimeController * m_timeController;
};

// Phyre::PAnimation::PAnimationDataSource  size=12  descriptor=0xcabd28
struct PAnimationDataSource {
  unsigned __int8 _pad_0[8];
  PAnimationSet * m_animationSet;
};

// Phyre::PAnimation::PAnimationDataSourceListEntry  size=148  descriptor=0xcacd18
struct PAnimationDataSourceListEntry {
  PAnimationDataSource * m_dataSource;
  unsigned int m_inputCount;
  unsigned __int8 _pad_8[128];
  unsigned int m_destSlotArray;
  unsigned int m_persistentBufferOffset;
  unsigned int m_preprocessBufferOffset;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationDataSourceListEntry,4>  size=None  descriptor=0xcad4b8
struct PAnimationDataSourceListEntry_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationDataSource *,4>  size=None  descriptor=0xcac748
struct PAnimationDataSource___4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationEvent  size=8  descriptor=0xcab4e8
struct PAnimationEvent {
  float m_time;
  unsigned int m_id;
};

// Phyre::PAnimation::PAnimationEventController  size=20  descriptor=0xcabe20
struct PAnimationEventController {
  unsigned __int8 _pad_0[12];
  PAnimationEventList * m_animationEventList;
  PTimeController * m_timeController;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationEvent,4>  size=None  descriptor=0xcab6a8
struct PAnimationEvent_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationNetworkInstanceTarget,4>  size=None  descriptor=0xcad398
struct PAnimationNetworkInstanceTarget_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationSlotArray,4>  size=None  descriptor=0xcad158
struct PAnimationSlotArray_4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationSlotFilter  size=32  descriptor=0xcac150
struct PAnimationSlotFilter {
  unsigned __int8 _pad_0[20];
  PAnimationDataSource m_animDataSource;
};

// Phyre::PAnimation::PAnimationSlotFilterDeferredLoad  size=8  descriptor=0xcac0b8
struct PAnimationSlotFilterDeferredLoad {
  unsigned __int8 m_keyType[4];  // PAnimationKeyDataType
  PString m_nodeName;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationSlotFilterDeferredLoad,4>  size=None  descriptor=0xcac2d8
struct PAnimationSlotFilterDeferredLoad_4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationSlotListIndex  size=16  descriptor=0xca9778
struct PAnimationSlotListIndex {
  unsigned __int8 m_animKeyType[8];  // PAnimationKeyDataType
  unsigned int m_targetIndex;
  unsigned int m_nodeCentricReIndex;
};

// Phyre::PArray<Phyre::PAnimation::PAnimationSlotListIndex,4>  size=None  descriptor=0xcab058
struct PAnimationSlotListIndex_4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PAnimationSpuTargetBlenderController  size=16  descriptor=0xcacb00
struct PAnimationSpuTargetBlenderController {
  unsigned __int8 _pad_0[12];
  PAnimationDataSource * m_animDataSource;
};

// Phyre::PArray<float,4>  size=None  descriptor=0xca9ab0
struct PArray_float_4_ {
  unsigned int m_count;
};

// Phyre::PArray<int,4>  size=None  descriptor=0xc99f70
struct PArray_int_4_ {
  unsigned int m_count;
};

// Phyre::PArray<unsigned char,1>  size=None  descriptor=0xc915c8
struct PArray_unsigned_char_1_ {
  unsigned int m_count;
};

// Phyre::PArray<unsigned char,4>  size=None  descriptor=0xc9ad48
struct PArray_unsigned_char_4_ {
  unsigned int m_count;
};

// Phyre::PArray<unsigned char const *,4>  size=None  descriptor=0xcbb760
struct PArray_unsigned_char_const___4_ {
  unsigned int m_count;
};

// Phyre::PArray<unsigned int,4>  size=None  descriptor=0xcac3f8
struct PArray_unsigned_int_4_ {
  unsigned int m_count;
};

// Phyre::PAssetReference  size=40  descriptor=0xc93f98
struct PAssetReference {
  unsigned __int8 _pad_0[24];
  PString m_id;
  void * m_asset;
  unsigned __int8 m_assetType[8];  // Phyre::PClassDescriptor
};

// Phyre::PAssetReferenceImport  size=12  descriptor=0xc94030
struct PAssetReferenceImport {
  PClassDescriptor * m_targetAssetType;
  PString m_id;
  unsigned __int8 _pad_8[4];
};

// Phyre::PTypedObject  size=8  descriptor=0xc90f30
struct PTypedObject {
  void * m_object;
  PClassDescriptor * m_classDescriptor;
};

// Phyre::PGameplay::PAttachableComponent  size=108  descriptor=0xcb0ac0
struct PAttachableComponent {
  unsigned __int8 _pad_0[12];
  PMat4 m_offsetMatrix;
  PTypedObject m_targetObject;
  PString m_targetName;
  PNode * m_nodeToUpdate;
  int m_boneIndex;
  PTypedObject m_attachedObject;
  bool m_isEnabled;
  unsigned __int8 _pad_69[3];
};

// Phyre::PArray<Phyre::PBase const *,4>  size=None  descriptor=0xcbbd20
struct PBase_const___4_ {
  unsigned int m_count;
};

// Phyre::PText::PBitmapFont  size=36  descriptor=0x1a84d20
struct PBitmapFont {
  bool m_isSDF;
  unsigned __int8 _pad_1[3];
  unsigned int m_fontSize;
  float m_lineSpacing;
  float m_baselineOffset;
  unsigned __int8 _pad_10[16];
  PTexture2D * m_bitmapFontTexture;
};

// Phyre::PText::PBitmapFontCharInfo  size=48  descriptor=0x1a84c88
struct PBitmapFontCharInfo {
  int m_characterCode;
  int m_kernPairs;
  int m_kernOffset;
  unsigned __int8 _pad_c[8];
  float m_width;
  float m_height;
  unsigned __int8 _pad_1c[16];
  bool m_rotated;
  unsigned __int8 _pad_2d[3];
};

// Phyre::PArray<Phyre::PText::PBitmapFontCharInfo,4>  size=None  descriptor=0x1a850b0
struct PBitmapFontCharInfo_4_ {
  unsigned int m_count;
};

// Phyre::PAnimation::PTimeIntervalController  size=24  descriptor=0xcaba08
struct PTimeIntervalController {
  unsigned __int8 _pad_0[8];
  PTimeController * m_parent;
  float m_parentBase;
  float m_localBase;
  float m_localRange;
};

// Phyre::PAnimation::PTimeScaleOffsetController  size=20  descriptor=0xcabba8
struct PTimeScaleOffsetController {
  unsigned __int8 _pad_0[8];
  PTimeController * m_parent;
  float m_scale;
  float m_offset;
};

// Phyre::PAnimation::PBlendableAnimationSource  size=76  descriptor=0xcad9a8
struct PBlendableAnimationSource {
  PAnimationClip * m_animationClip;
  float m_weight;
  float m_speed;
  PTimeIntervalController m_timeIntervalController;
  PTimeScaleOffsetController m_timeScaleOffsetController;
  PAnimationController m_animationController;
};

// Phyre::PArray<Phyre::PAnimation::PBlendableAnimationSource,4>  size=None  descriptor=0xcae060
struct PBlendableAnimationSource_4_ {
  unsigned int m_count;
};

// Phyre::PCamera  size=332  descriptor=0xc922c0
struct PCamera {
  PMat4x3 m_viewMatrix;
  PMat4 m_projectionMatrix;
  PMat4 m_viewProjectionMatrix;
  PWorldMatrix * m_localToWorldMatrix;
  float m_nearPlane;
  float m_farPlane;
  PVec4f m_ambientColor;
  PString m_name;
  PVec4f m_fogColor;
  float m_fogNear;
  float m_fogFar;
  float m_fogCurve;
  float m_fogLimit;
  PVec4f m_bgColor;
  float m_glowUpperThreshold;
  float m_glowLowerThreshold;
  float m_glowGain;
  float m_glowSpread;
  float m_dofFocusPlaneDistance;
  float m_dofFocusRange;
  float m_dofFocusBlurRange;
  PVec4f m_charcterLightIntensity;
  PVec4f m_charcterLightPitch;
  PVec4f m_characterLightYaw;
};

// Phyre::PPhysics::PPhysicsCharacterCamera  size=512  descriptor=0xcbe940
struct PPhysicsCharacterCamera {
  unsigned __int8 _pad_0[8];
  float m_collisionRadius;
  float m_targetDistance;
  float m_targetHeight;
  float m_contactEpsilon;
  float m_minimumCameraDistance;
  float m_smoothingRate;
  unsigned __int8 _pad_20[8];
  float m_springTension;
  float m_nearCameraYAdjust;
  unsigned __int8 _pad_30[464];
};

// Phyre::PGameplay::PCameraControllerComponent  size=704  descriptor=0xcaff10
struct PCameraControllerComponent {
  unsigned __int8 _pad_0[48];
  unsigned __int8 m_camera[128];  // Phyre::PCameraProjection
  bool m_isMainCamera;
  bool m_isFollowCamera;
  unsigned __int8 _pad_b2[2];
  PWorldMatrix * m_followTarget;
  PNode * m_nodeToUpdate;
  PPhysicsCharacterCamera m_followPhysics;
  unsigned __int8 _pad_2bc[4];
};

// Phyre::PCameraOrthographic  size=344  descriptor=0xc930e8
struct PCameraOrthographic {
  unsigned __int8 _pad_0[340];
  float m_height;
};

// Phyre::PCameraPerspective  size=344  descriptor=0xc93608
struct PCameraPerspective {
  unsigned __int8 _pad_0[340];
  float m_FOV;
};

// Phyre::PCameraProjection  size=340  descriptor=0xc92358
struct PCameraProjection {
  unsigned __int8 _pad_0[336];
  float m_aspect;
};

// Phyre::PScripting::PClassCallableMethodScript  size=52  descriptor=0xcc9a40
struct PClassCallableMethodScript {
  unsigned __int8 _pad_0[40];
  PScript * m_script;
  PString m_nameAsString;
  PString m_entryPoint;
};

// Phyre::PClassDataMemberDynamic  size=48  descriptor=0xc91180
struct PClassDataMemberDynamic {
  unsigned __int8 _pad_0[24];
  void * m_type;
  unsigned __int8 _pad_1c[8];
  unsigned int m_offset;
  unsigned __int8 _pad_28[4];
  PString m_nameAsString;
};

// Phyre::PClassDescriptorDynamic  size=184  descriptor=0xc91330
struct PClassDescriptorDynamic {
  unsigned __int8 _pad_0[28];
  unsigned int m_size;
  unsigned __int8 _pad_20[32];
  unsigned __int8 m_parent[68];  // Phyre::PClassDescriptor
  int m_offsetFromParent;
  int m_offsetToBase;
  int m_offsetToBaseInAllocatedBlock;
  unsigned __int8 _pad_90[6];
  unsigned __int16 m_currentMemberCount;
  unsigned int m_actualSize;
  unsigned __int8 _pad_9c[24];
  PString m_nameAsString;
};

// Phyre::PClassMember  size=20  descriptor=0xc90610
struct PClassMember {
  unsigned __int8 _pad_0[8];
  unsigned __int8 m_classDescriptor[8];  // Phyre::PClassDescriptor
  unsigned int m_flags;
};

// Phyre::PSerialization::PBinary::PClusterHeaderBase  size=72  descriptor=0xc96aa8
struct PClusterHeaderBase {
  unsigned int m_phyreMarker;
  unsigned int m_size;
  unsigned int m_packedNamespaceSize;
  unsigned int m_platformID;
  unsigned int m_instanceListCount;
  unsigned int m_arrayFixupSize;
  unsigned int m_arrayFixupCount;
  unsigned int m_pointerFixupSize;
  unsigned int m_pointerFixupCount;
  unsigned int m_pointerArrayFixupSize;
  unsigned int m_pointerArrayFixupCount;
  unsigned int m_pointersInArraysCount;
  unsigned int m_userFixupCount;
  unsigned int m_userFixupDataSize;
  unsigned int m_totalDataSize;
  unsigned int m_headerClassInstanceCount;
  unsigned int m_headerClassChildCount;
  unsigned int m_physicsEngineID;
};

// Phyre::PSerialization::PBinary::PClusterHeaderD3D11  size=84  descriptor=0xc970f8
struct PClusterHeaderD3D11 {
  unsigned __int8 _pad_0[72];
  unsigned int m_indexBufferSize;
  unsigned int m_vertexBufferSize;
  unsigned int m_maxTextureBufferSize;
};

// Phyre::PComponent  size=12  descriptor=0xc91c18
struct PComponent {
  PComponent * m_next;
  PEntity * m_entity;
  PClassDescriptor * m_componentType;
};

// Phyre::PRendering::PConstantBufferBase  size=12  descriptor=0xc9b4c8
struct PConstantBufferBase {
  unsigned int m_size;
  unsigned __int8 _pad_4[8];
};

// Phyre::PArray<Phyre::PRendering::PContextSwitch const *,4>  size=None  descriptor=0xca6ab0
struct PContextSwitch_const___4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PContextVariantFoldingTable  size=20  descriptor=0xca5660
struct PContextVariantFoldingTable {
  unsigned int m_contextVariantIndex;
  unsigned int m_contextVariantVpIndex;
  unsigned int m_contextVariantFpIndex;
  unsigned int m_contextVariantGsIndex;
  unsigned int m_contextVariantCsIndex;
};

// Phyre::PArray<Phyre::PRendering::PContextVariantFoldingTable,4>  size=None  descriptor=0xca5aa0
struct PContextVariantFoldingTable_4_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PDataBlockBase  size=24  descriptor=0xc988b0
struct PDataBlockBase {
  unsigned int m_stride;
  unsigned int m_elementCount;
  unsigned __int8 _pad_8[13];
  unsigned __int8 m_memoryType;
  unsigned __int8 _pad_16[2];
};

// Phyre::PSharray<Phyre::PGeometry::PDataBlockBufferD3D11>  size=None  descriptor=0xcb16a8
struct PDataBlockBufferD3D11_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PDataBlockD3D11  size=64  descriptor=0xcb1250
struct PDataBlockD3D11 {
  unsigned __int8 _pad_0[48];
  unsigned int m_offsetInVertexBuffer;
  unsigned __int8 _pad_34[4];
  unsigned int m_dataSize;
  unsigned __int8 _pad_3c[4];
};

// Phyre::PArray<Phyre::PGeometry::PDataBlockD3D11,4>  size=None  descriptor=0xcb1468
struct PDataBlockD3D11_4_ {
  unsigned int m_count;
};

// Phyre::PPostProcessing::PDeferredLightingBase  size=16276  descriptor=0x1943680
struct PDeferredLightingBase {
  unsigned __int8 _pad_0[16016];
  PVec4f m_ambientColor;
  PVec4f m_fogColor;
  float m_fogDistance;
  float m_fogNearDistance;
  float m_fogAmount;
  unsigned __int8 _pad_3ebc[12];
  float m_instantLightIntensity;
  float m_instantLightScatteringIntensity;
  unsigned __int8 _pad_3ed0[196];
};

// Phyre::PPostProcessing::PDepthOfFieldBase  size=4016  descriptor=0x1943848
struct PDepthOfFieldBase {
  unsigned __int8 _pad_0[20];
  float m_focusPlaneDistance;
  float m_focusRange;
  float m_focusBlurRange;
  unsigned __int8 _pad_20[3984];
};

// Phyre::PGeometry::PDynamicDataBlock  size=24  descriptor=0xc98d18
struct PDynamicDataBlock {
  unsigned int m_stride;
  unsigned int m_elementCount;
  unsigned __int8 _pad_8[16];
};

// Phyre::PGeometry::PDynamicMesh  size=28  descriptor=0xc9a7d0
struct PDynamicMesh {
  unsigned __int8 _pad_0[4];
  unsigned __int8 m_mesh[24];  // Phyre::PGeometry::PMesh
};

// Phyre::PRendering::PDynamicMeshInstance  size=12  descriptor=0xca7f28
struct PDynamicMeshInstance {
  unsigned __int8 _pad_0[4];
  unsigned __int8 m_dynamicMesh[8];  // Phyre::PGeometry::PDynamicMesh
};

// Phyre::PGeometry::PDynamicMesh::PDynamicSegmentDesc  size=16  descriptor=0xc9a738
struct PDynamicSegmentDesc {
  unsigned int m_startStreamIndex;
  unsigned int m_streamCount;
  unsigned int m_elementCount;
  int m_indexCount;
};

// Phyre::PArray<Phyre::PGeometry::PDynamicMesh::PDynamicSegmentDesc,4>  size=None  descriptor=0xc9aa10
struct PDynamicSegmentDesc_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PEffect  size=64  descriptor=0xca66e8
struct PEffect {
  unsigned int m_supportedLightMask;
  unsigned int m_supportedShadowCasterMask;
  PString m_effectFile;
  unsigned __int8 _pad_c[40];
  PString m_effectSource;
  unsigned int m_maxLightCount;
  unsigned int m_numSupportedShaderLODLevels;
};

// Phyre::PRendering::PEffectVariant  size=52  descriptor=0xca5ff0
struct PEffectVariant {
  unsigned __int8 m_effect[28];  // Phyre::PRendering::PEffect
  unsigned __int16 m_largestShaderPassCount;
  unsigned __int8 _pad_1e[18];
  unsigned __int16 m_tweakableParameterBufferSize;
  unsigned __int16 m_untweakableParameterBufferSize;
};

// Phyre::PArray<Phyre::PRendering::PEffectVariant *,4>  size=None  descriptor=0xca6e10
struct PEffectVariant___4_ {
  unsigned int m_count;
};

// Phyre::PEntity  size=28  descriptor=0xc91dd8
struct PEntity {
  PComponent * m_firstComponent;
  unsigned __int8 m_worldMatrix[24];  // Phyre::PWorldMatrix
};

// Phyre::PGame::PGameSettings  size=12  descriptor=0x1a855b8
struct PGameSettings {
  unsigned __int8 _pad_0[8];
  PInputMap * m_inputMap;
};

// Phyre::PPostProcessing::PGlowBase  size=144  descriptor=0x1944050
struct PGlowBase {
  unsigned __int8 _pad_0[104];
  float m_glowAmountScale;
  float m_glowLuminanceThreshold;
  float m_glowLuminanceScale;
  unsigned __int8 _pad_74[28];
};

// Phyre::PGeometry::PIndexDataBlockBase  size=20  descriptor=0xc98ec8
struct PIndexDataBlockBase {
  unsigned int m_minimumIndex;
  unsigned int m_maximumIndex;
  unsigned int m_elementCount;
  unsigned __int8 m_type;
  unsigned __int8 m_memoryType;
  unsigned __int8 _pad_e[6];
};

// Phyre::PSharray<Phyre::PGeometry::PIndexDataBlockBufferD3D11>  size=None  descriptor=0xcb1588
struct PIndexDataBlockBufferD3D11_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PIndexDataBlockD3D11  size=60  descriptor=0xcb1120
struct PIndexDataBlockD3D11 {
  unsigned __int8 _pad_0[44];
  unsigned int m_offsetInIndexBuffer;
  unsigned __int8 _pad_30[4];
  unsigned int m_dataSize;
  unsigned __int8 _pad_38[4];
};

// Phyre::PInputs::PInputAction  size=16  descriptor=0x1941878
struct PInputAction {
  unsigned __int8 _pad_0[4];
  PString m_name;
  unsigned __int8 _pad_8[8];
};

// Phyre::PArray<Phyre::PInputs::PInputAction *,4>  size=None  descriptor=0x1941e98
struct PInputAction___4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PInputs::PInputMap *,4>  size=None  descriptor=0x1a856b0
struct PInputMap___4_ {
  unsigned int m_count;
};

// Phyre::PFramework::PInputMapper  size=184  descriptor=0xcc9e50
struct PInputMapper {
  PInputMap m_inputMap;
  unsigned __int8 _pad_8[176];
};

// Phyre::PInputs::PInputSource  size=24  descriptor=0x1940ad0
struct PInputSource {
  unsigned __int8 _pad_0[8];
  unsigned __int8 m_typeSemantic[4];  // PInputTypeSemantic
  unsigned int m_id;
  bool m_invert;
  unsigned __int8 _pad_11[3];
  float m_scale;
};

// Phyre::PInputs::PInputSourceJoypadAxis  size=32  descriptor=0x1940c98
struct PInputSourceJoypadAxis {
  unsigned __int8 _pad_0[28];
  unsigned __int8 m_joypadAxisSemantic[4];  // PInputAxisSemantic
};

// Phyre::PInputs::PInputSourceJoypadButton  size=40  descriptor=0x1940c00
struct PInputSourceJoypadButton {
  unsigned __int8 _pad_0[32];
  unsigned __int8 m_positiveButtonSemantic[4];  // PInputJoypadButtonSemantic
  unsigned __int8 m_negativeButtonSemantic[4];  // PInputJoypadButtonSemantic
};

// Phyre::PInputs::PInputSourceKey  size=40  descriptor=0x1940b68
struct PInputSourceKey {
  unsigned __int8 _pad_0[32];
  unsigned __int8 m_positiveKeySemantic[4];  // PInputKeySemantic
  unsigned __int8 m_negativeKeySemantic[4];  // PInputKeySemantic
};

// Phyre::PInputs::PInputSourceMouseButton  size=32  descriptor=0x1940d30
struct PInputSourceMouseButton {
  unsigned __int8 _pad_0[28];
  unsigned __int8 m_mouseButtonSemantic[4];  // PInputMouseButtonSemantic
};

// Phyre::PInputs::PInputSourceMouseDeltaX  size=40  descriptor=0x1940dc8
struct PInputSourceMouseDeltaX {
  unsigned __int8 _pad_0[32];
  unsigned __int8 m_modifierButtonSemantic[4];  // PInputMouseButtonSemantic
  unsigned __int8 m_modifierKeySemantic[4];  // PInputKeySemantic
};

// Phyre::PInputs::PInputSourceMouseDeltaY  size=40  descriptor=0x1940e60
struct PInputSourceMouseDeltaY {
  unsigned __int8 _pad_0[32];
  unsigned __int8 m_modifierButtonSemantic[4];  // PInputMouseButtonSemantic
  unsigned __int8 m_modifierKeySemantic[4];  // PInputKeySemantic
};

// Phyre::PArray<Phyre::PInputs::PInputSource *,4>  size=None  descriptor=0x1941d78
struct PInputSource___4_ {
  unsigned int m_count;
};

// Phyre::PSerialization::PBinary::PInternal::PInstanceListHeader  size=36  descriptor=0xc97338
struct PInstanceListHeader {
  unsigned int m_classID;
  unsigned int m_count;
  unsigned int m_size;
  unsigned int m_objectsSize;
  unsigned int m_arraysSize;
  unsigned int m_pointersInArraysCount;
  unsigned int m_arrayFixupCount;
  unsigned int m_pointerFixupCount;
  unsigned int m_pointerArrayFixupCount;
};

// Phyre::PLOD::PLODGroup  size=72  descriptor=0xcbab20
struct PLODGroup {
  PVec4f m_minBounds;
  PVec4f m_maxBounds;
  unsigned __int8 _pad_20[8];
  float m_blendRange;
  float m_shaderLODLevelDistance;
  unsigned __int8 _pad_30[4];
  unsigned __int8 m_lodMetricType[4];  // PLODMetricType
  unsigned __int8 m_lodBlendType[12];  // PLODBlendType
  bool m_isEnabled;
  unsigned __int8 _pad_45[3];
};

// Phyre::PLOD::PLODLevel  size=36  descriptor=0xcbaa88
struct PLODLevel {
  unsigned __int8 m_lodGroup[12];  // Phyre::PLOD::PLODGroup
  float m_minimumThreshold;
  float m_maximumThreshold;
  unsigned __int8 _pad_14[16];
};

// Phyre::PArray<Phyre::PLOD::PLODLevel,4>  size=None  descriptor=0xcbaf50
struct PLODLevel_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PLight  size=52  descriptor=0xc9af80
struct PLight {
  PVec4f m_color;
  PWorldMatrix * m_localToWorldMatrix;
  PShadowCaster * m_shadowCaster;
  void * m_lightType;
  float m_innerConeAngle;
  float m_outerConeAngle;
  float m_intensity;
  float m_innerRange;
  float m_outerRange;
  float m_scatteringIntensity;
};

// Phyre::PArray<Phyre::PRendering::PLightType const *,4>  size=None  descriptor=0xca6cf0
struct PLightType_const___4_ {
  unsigned int m_count;
};

// Phyre::PGameplay::PLocator  size=12  descriptor=0xcae550
struct PLocator {
  PWorldMatrix * m_localToWorldMatrix;
  PString m_name;
  void * m_shapeType;
};

// Phyre::PRendering::PMaterial  size=24  descriptor=0xca5268
struct PMaterial {
  PEffectVariant * m_effectVariant;
  PParameterBuffer * m_parameterBuffer;
  unsigned __int8 m_remapFrom[4];  // PSceneRenderPassType
  unsigned __int8 m_remapTo[4];  // PSceneRenderPassType
  PString m_texAnimID;
  PShaderParameterDefinition * m_chLightAmt;
};

// Phyre::PRendering::PMaterialSwitch  size=8  descriptor=0xca51d0
struct PMaterialSwitch {
  PString m_name;
  PString m_value;
};

// Phyre::PArray<Phyre::PRendering::PMaterialSwitch,4>  size=None  descriptor=0xca6480
struct PMaterialSwitch_4_ {
  unsigned int m_count;
};

// Phyre::PSharray<Phyre::PRendering::PMaterial *>  size=None  descriptor=0xc99a30
struct PMaterial___ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PMatrix4,4>  size=None  descriptor=0xc9a1b0
struct PMatrix4_4_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PMesh  size=56  descriptor=0xc99c80
struct PMesh {
  unsigned __int8 _pad_0[48];
  PMaterialSet m_defaultMaterials;
};

// Phyre::PRendering::PMeshInstance  size=132  descriptor=0xca7228
struct PMeshInstance {
  PMesh * m_mesh;
  unsigned __int8 m_localToWorldMatrix[12];  // Phyre::PWorldMatrix
  PMaterialSet * m_materialSet;
  PMeshSegment * m_instanceSegment;
  PDynamicMeshInstance * m_dynamicMeshInstance;
  PMeshInstanceBounds * m_bounds;
  unsigned __int8 m_lodLevel[12];  // Phyre::PLOD::PLODLevel
  PString m_name;
  int m_animCt;
  int m_animID0;
  int m_animID1;
  int m_animID2;
  int m_animID3;
  int m_mimeCt;
  int m_groupID;
  int m_DObjKind;
  int m_RotType;
  int m_dObjFlag;
  int m_objID;
  float m_layerz0;
  float m_layerz1;
  float m_layerz2;
  float m_layerz3;
  unsigned int m_flags;
  unsigned __int8 _pad_70[20];
};

// Phyre::PRendering::PMeshInstanceAttachPoint  size=12  descriptor=0xca8bd8
struct PMeshInstanceAttachPoint {
  PWorldMatrix * m_destination;
  PMeshInstance * m_source;
  unsigned int m_sourceMatrixIndex;
};

// Phyre::PRendering::PMeshInstanceBounds  size=36  descriptor=0xca89b8
struct PMeshInstanceBounds {
  unsigned __int8 _pad_0[12];
  unsigned __int8 m_worldMatrix[16];  // Phyre::PWorldMatrix
  PMeshInstance * m_meshInstance;
  int m_render_order;
};

// Phyre::PArray<Phyre::PRendering::PMeshInstanceSegmentContext,4>  size=None  descriptor=0xca7ac8
struct PMeshInstanceSegmentContext_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PMeshInstanceSegmentStreamBinding  size=12  descriptor=0xca70f8
struct PMeshInstanceSegmentStreamBinding {
  unsigned __int8 m_renderDataType[4];  // PRenderDataType
  PString m_name;
  unsigned __int16 m_nameHash;
  unsigned __int8 m_index;
  unsigned __int8 m_inputSet;
};

// Phyre::PSharray<Phyre::PRendering::PMeshInstanceSegmentStreamBinding const *>  size=None  descriptor=0xca79a8
struct PMeshInstanceSegmentStreamBinding_const___ {
  unsigned int m_count;
};

// Phyre::PSharray<Phyre::PRendering::PMeshInstance *>  size=None  descriptor=0xcbae30
struct PMeshInstance___ {
  unsigned int m_count;
};

// Phyre::PGeometry::PMeshSegmentBase  size=40  descriptor=0xc99298
struct PMeshSegmentBase {
  unsigned int m_materialIndex;
  int m_matrixIndex;
  unsigned __int8 _pad_8[16];
  int m_modeIndex;
  int m_segmentRenderOrder;
  int m_clothmodelIndex;
  int m_clothIndex;
};

// Phyre::PGeometry::PMeshSegmentD3D11  size=108  descriptor=0xcb0ff0
struct PMeshSegmentD3D11 {
  unsigned __int8 _pad_0[48];
  PIndexDataBlockD3D11 m_indexData;
};

// Phyre::PArray<Phyre::PGeometry::PMeshSegment,4>  size=None  descriptor=0xc9a3f0
struct PMeshSegment_4_ {
  unsigned int m_count;
};

// Phyre::PDynamicGeometry::PModifierAndInputs  size=20  descriptor=0xcbc128
struct PModifierAndInputs {
  unsigned __int8 m_modifier[4];  // PModifier
  unsigned int m_persistentStateOffset;
  unsigned int m_stateBlockOffset;
  unsigned __int8 _pad_c[8];
};

// Phyre::PArray<Phyre::PDynamicGeometry::PModifierAndInputs,4>  size=None  descriptor=0xcbce60
struct PModifierAndInputs_4_ {
  unsigned int m_count;
};

// Phyre::PDynamicGeometry::PModifierNetwork  size=64  descriptor=0xcbc420
struct PModifierNetwork {
  unsigned int m_totalOutputElementSize;
  unsigned __int8 _pad_4[8];
  unsigned int m_totalInputCount;
  unsigned __int8 _pad_10[24];
  PModifierNetworkInfoPacket * m_infoPacket;
  unsigned int m_totalPersistentStateSize;
  unsigned int m_totalStateBlockSize;
  bool m_isCompiled;
  unsigned __int8 _pad_35[11];
};

// Phyre::PDynamicGeometry::PModifierNetworkBuffer  size=20  descriptor=0xcbc090
struct PModifierNetworkBuffer {
  unsigned __int8 m_type[4];  // PRenderDataType
  unsigned int m_modifier;
  unsigned int m_stream;
  unsigned int m_elementSize;
  unsigned int m_info;
};

// Phyre::PArray<Phyre::PDynamicGeometry::PModifierNetworkBuffer,4>  size=None  descriptor=0xcbcd40
struct PModifierNetworkBuffer_4_ {
  unsigned int m_count;
};

// Phyre::PDynamicGeometry::PModifierNetworkDynamicMeshSegment  size=16  descriptor=0xcbb268
struct PModifierNetworkDynamicMeshSegment {
  PModifierNetwork * m_modifierNetwork;
  unsigned int m_segmentInputGroupStart;
  unsigned int m_segmentOutputGroupStart;
  unsigned int m_modifierOutputsAreConsumed;
};

// Phyre::PArray<Phyre::PDynamicGeometry::PModifierNetworkDynamicMeshSegment,4>  size=None  descriptor=0xcbb520
struct PModifierNetworkDynamicMeshSegment_4_ {
  unsigned int m_count;
};

// Phyre::PDynamicGeometry::PModifierNetworkInfoPacket  size=32  descriptor=0xcbc388
struct PModifierNetworkInfoPacket {
  unsigned int m_size;
  unsigned int m_modifierCodeCount;
  unsigned int m_modifierInstanceCount;
  unsigned __int16 m_inputCount;
  unsigned __int16 m_outputCount;
  unsigned __int8 _pad_10[4];
  unsigned int m_totalPersistentStateSize;
  unsigned int m_totalStateBlockSize;
  unsigned int m_spuModifierCodeSize;
};

// Phyre::PDynamicGeometry::PModifierNetworkInfoPacket_Buffer  size=2  descriptor=0xcbc2f0
struct PModifierNetworkInfoPacket_Buffer {
  unsigned __int16 m_elementSize;
};

// Phyre::PDynamicGeometry::PModifierNetworkInfoPacket_ModifierCode  size=32  descriptor=0xcbc1c0
struct PModifierNetworkInfoPacket_ModifierCode {
  unsigned __int8 m_modifier[4];  // PModifier
  unsigned int m_inputCount;
  unsigned int m_outputCount;
  unsigned __int8 _pad_c[20];
};

// Phyre::PDynamicGeometry::PModifierNetworkInfoPacket_ModifierInstance  size=32  descriptor=0xcbc258
struct PModifierNetworkInfoPacket_ModifierInstance {
  unsigned int m_persistentStateOffset;
  unsigned int m_stateBlockOffset;
  unsigned __int8 m_codeIndex;
  unsigned __int8 _pad_9[23];
};

// Phyre::PDynamicGeometry::PModifierNetworkInstance  size=20  descriptor=0xcbbb38
struct PModifierNetworkInstance {
  unsigned __int8 m_modifierNetwork[20];  // Phyre::PDynamicGeometry::PModifierNetwork
};

// Phyre::PDynamicGeometry::PRenderStream  size=8  descriptor=0xcbbff8
struct PRenderStream {
  PDynamicDataBlock * m_dataBlock;
  PVertexStream * m_vertexStream;
};

// Phyre::PDynamicGeometry::PModifierNetworkInstanceInput  size=20  descriptor=0xcbba08
struct PModifierNetworkInstanceInput {
  unsigned __int8 _pad_0[8];
  unsigned int m_elementSize;
  PRenderStream m_renderStream;
};

// Phyre::PDynamicGeometry::PModifierNetworkInstancePacketInput  size=8  descriptor=0xcbbaa0
struct PModifierNetworkInstancePacketInput {
  unsigned int m_source;
  unsigned int m_list;
};

// Phyre::PArray<Phyre::PDynamicGeometry::PModifierNetworkInstance *,4>  size=None  descriptor=0xca80f0
struct PModifierNetworkInstance___4_ {
  unsigned int m_count;
};

// Phyre::PPostProcessing::PMotionBlurBase  size=3404  descriptor=0x1943a10
struct PMotionBlurBase {
  unsigned __int8 _pad_0[3284];
  float m_velocityScale;
  unsigned __int8 _pad_cd8[116];
};

// Phyre::PNameComponent  size=24  descriptor=0xc93e38
struct PNameComponent {
  unsigned __int8 _pad_0[20];
  PString m_name;
};

// Phyre::PScene::PNode  size=84  descriptor=0xca91f8
struct PNode {
  PNode * m_next;
  PNode * m_parent;
  PNode * m_firstChild;
  PWorldMatrix * m_worldMatrix;
  PMat4 m_localMatrix;
  PString m_name;
};

// Phyre::PArray<Phyre::PRendering::PNodeContext,4>  size=None  descriptor=0xca6990
struct PNodeContext_4_ {
  unsigned int m_count;
};

// Phyre::POccluderGeometry::POccluderGeometryInstance  size=8  descriptor=0x1a85420
struct POccluderGeometryInstance {
  POccluderGeometryObject * m_occluder;
  PWorldMatrix * m_localToWorldMatrix;
};

// Phyre::POccluderGeometry::POccluderGeometryObject  size=48  descriptor=0x1a85990
struct POccluderGeometryObject {
  unsigned int m_planeCount;
  unsigned int m_vertexCount;
  unsigned int m_edgeCount;
  unsigned __int8 _pad_c[4];
  PVec4f m_minBounds;
  PVec4f m_maxBounds;
};

// Phyre::PRendering::PParameterBuffer  size=16  descriptor=0xca5040
struct PParameterBuffer {
  unsigned __int8 _pad_0[4];
  unsigned __int8 m_effectVariant[12];  // Phyre::PRendering::PEffectVariant
};

// Phyre::PRendering::PParameterBufferBase  size=4  descriptor=0xca4fa8
struct PParameterBufferBase {
  unsigned int m_parameterBufferSize;
};

// Phyre::PPhysics::PPhysicsBoxBase  size=108  descriptor=0xcbe6e0
struct PPhysicsBoxBase {
  unsigned __int8 _pad_0[92];
  PVec4f m_halfExtents;
};

// Phyre::PPhysics::PPhysicsCallbackData  size=20  descriptor=0xcbf190
struct PPhysicsCallbackData {
  PTypedObject m_collisionObjectA;
  PTypedObject m_collisionObjectB;
  float m_amount;
};

// Phyre::PPhysics::PPhysicsCapsuleBase  size=108  descriptor=0xcbe810
struct PPhysicsCapsuleBase {
  unsigned __int8 _pad_0[100];
  float m_height;
  unsigned __int8 _pad_68[4];
};

// Phyre::PScripting::PScriptCallbackHandler  size=12  descriptor=0xcbe2f0
struct PScriptCallbackHandler {
  PString m_entryPoint;
  PTypedObject m_handler;
};

// Phyre::PPhysics::PPhysicsCharacterControllerBase  size=268  descriptor=0xcbe9d8
struct PPhysicsCharacterControllerBase {
  PPhysicsCharacterControllerBase * m_next;
  PVec4f m_startPosition;
  PVec4f m_scale;
  PVec4f m_gravity;
  unsigned __int8 _pad_34[16];
  PMat4 m_graphicsOffset;
  PMat4 m_invGraphicsOffset;
  PNode * m_targetNode;
  PWorldMatrix * m_targetWorldMatrix;
  PPhysicsWorld * m_world;
  float m_rotate;
  float m_right;
  float m_forward;
  float m_velocity;
  float m_maxSlopeAngle;
  float m_jumpHeight;
  float m_height;
  float m_radius;
  unsigned __int8 _pad_f0[4];
  bool m_jump;
  bool m_isOnGround;
  unsigned __int8 _pad_f6[10];
  PScriptCallbackHandler m_scriptHandler;
};

// Phyre::PPhysics::PPhysicsCylinderBase  size=104  descriptor=0xcbeb08
struct PPhysicsCylinderBase {
  unsigned __int8 _pad_0[100];
  float m_height;
};

// Phyre::PPhysics::PPhysicsMaterial  size=12  descriptor=0xcbed68
struct PPhysicsMaterial {
  float m_dynamicFriction;
  float m_staticFriction;
  float m_restitution;
};

// Phyre::PPhysics::PPhysicsMeshBase  size=96  descriptor=0xcbee00
struct PPhysicsMeshBase {
  unsigned __int8 _pad_0[92];
  PShape * m_shape;
};

// Phyre::PPhysics::PPhysicsModel  size=12  descriptor=0xcbef30
struct PPhysicsModel {
  PPhysicsModel * m_next;
  PPhysicsRigidBody * m_rigidBodies;
  PPhysicsWorld * m_world;
};

// Phyre::PPhysics::PPhysicsPlaneBase  size=108  descriptor=0xcbefc8
struct PPhysicsPlaneBase {
  unsigned __int8 _pad_0[92];
  PVec4f m_equationCoefficient;
};

// Phyre::PPhysics::PPhysicsRigidBodyBase  size=220  descriptor=0xcbf0f8
struct PPhysicsRigidBodyBase {
  PPhysicsRigidBody * m_next;
  PMat4x3 m_massFrameTransform;
  PVec4f m_initialPosition;
  PVec4f m_initialOrientation;
  PVec4f m_inertiaTensor;
  PVec4f m_initialLinearVelocity;
  PVec4f m_initialAngularVelocity;
  PVec4f m_scale;
  PPhysicsMaterial * m_material;
  PNode * m_targetNode;
  unsigned __int8 m_targetWorldMatrix[12];  // Phyre::PWorldMatrix
  float m_linearDamping;
  float m_angularDamping;
  unsigned __int8 m_model[8];  // Phyre::PPhysics::PPhysicsModel
  PPhysicsRigidBody * m_nextKinematicRigidBody;
  unsigned __int8 m_collisionGroup;
  unsigned __int8 _pad_bd[3];
  float m_mass;
  unsigned __int8 m_rigidBodyType;
  bool m_enabled;
  unsigned __int8 _pad_c6[10];
  PScriptCallbackHandler m_scriptHandler;
};

// Phyre::PPhysics::PPhysicsShapeBase  size=88  descriptor=0xcbf2c0
struct PPhysicsShapeBase {
  unsigned __int8 _pad_0[4];
  bool m_hollow;
  unsigned __int8 _pad_5[3];
  float m_mass;
  float m_density;
  PPhysicsMaterial * m_material;
  PMat4x3 m_transform;
  PVec4f m_scale;
  unsigned __int8 _pad_54[4];
};

// Phyre::PSharray<Phyre::PPhysics::PPhysicsShape *>  size=None  descriptor=0xcc5558
struct PPhysicsShape___ {
  unsigned int m_count;
};

// Phyre::PPhysics::PPhysicsSphereBase  size=96  descriptor=0xcbf3f0
struct PPhysicsSphereBase {
  unsigned __int8 _pad_0[92];
  float m_radius;
};

// Phyre::PPhysics::PPhysicsTaperedCapsule  size=112  descriptor=0xcbf520
struct PPhysicsTaperedCapsule {
  unsigned __int8 _pad_0[108];
  float m_height;
};

// Phyre::PPhysics::PPhysicsTaperedCylinder  size=112  descriptor=0xcbf5b8
struct PPhysicsTaperedCylinder {
  unsigned __int8 _pad_0[108];
  float m_height;
};

// Phyre::PPhysics::PPhysicsWorldBase  size=84  descriptor=0xcbf650
struct PPhysicsWorldBase {
  PVec4f m_gravity;
  float m_timeStep;
  PVec4f m_worldMin;
  PVec4f m_worldMax;
  unsigned __int8 _pad_34[20];
  PPhysicsModel * m_models;
  PPhysicsCharacterControllerBase * m_characterControllers;
  PPhysicsRigidBody * m_kinematicRigidBodies;
};

// Phyre::PPostProcessing::PPostEffectBase  size=20  descriptor=0x19435e8
struct PPostEffectBase {
  unsigned __int8 _pad_0[8];
  unsigned __int8 m_effectMaterial[9];  // Phyre::PRendering::PMaterial
  bool m_enabled;
  unsigned __int8 _pad_12[2];
};

// Phyre::PArray<Phyre::PPostProcessing::PPostEffectBase *,4>  size=None  descriptor=0x1945010
struct PPostEffectBase___4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PQWord,16>  size=None  descriptor=0xcad278
struct PQWord_16_ {
  unsigned int m_count;
};

// Phyre::PWorldMatrix  size=48  descriptor=0xc92090
struct PWorldMatrix {
  PMat4x3 m_matrix;
};

// Phyre::PGameplay::PQuarryComponent  size=92  descriptor=0xcaf540
struct PQuarryComponent {
  unsigned __int8 _pad_0[12];
  PWorldMatrix m_previousLocalToWorldMatrix;
  PVec4f m_min;
  PVec4f m_size;
};

// Phyre::PPhysics::PRaycastResult  size=40  descriptor=0xcbf6e8
struct PRaycastResult {
  PVec4f m_contactPoint;
  PVec4f m_contactNormal;
  void * m_collisionObject;
  PClassDescriptor * m_collisionObjectClassDescriptor;
};

// Phyre::PDynamicGeometry::PRenderStreamInput  size=8  descriptor=0xcbbf60
struct PRenderStreamInput {
  int m_source;
  int m_stream;
};

// Phyre::PArray<Phyre::PDynamicGeometry::PRenderStreamInput,4>  size=None  descriptor=0xcbcc20
struct PRenderStreamInput_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PDynamicGeometry::PRenderStream,4>  size=None  descriptor=0xcbb640
struct PRenderStream_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PRenderTargetBase  size=16  descriptor=0xca4168
struct PRenderTargetBase {
  unsigned __int8 _pad_0[5];
  unsigned __int8 m_msaaType;
  unsigned __int8 _pad_6[2];
  int m_renderTargetFlags;
  unsigned __int8 _pad_c[4];
};

// Phyre::PRendering::PSamplerStateBase  size=32  descriptor=0xca2898
struct PSamplerStateBase {
  unsigned __int8 m_minFilter;
  unsigned __int8 m_magFilter;
  unsigned __int8 m_wrapS;
  unsigned __int8 m_wrapT;
  unsigned __int8 m_wrapR;
  unsigned __int8 _pad_5[3];
  float m_lodBias;
  float m_maxAnisotropy;
  unsigned int m_borderColor;
  unsigned int m_baseLevel;
  unsigned int m_maxLevel;
  unsigned int m_flags;
};

// Phyre::PRendering::PSceneRenderPass  size=40  descriptor=0xca5530
struct PSceneRenderPass {
  unsigned __int8 m_passType[36];  // ?
  bool m_platformsAreInclude;
  unsigned __int8 _pad_25[3];
};

// Phyre::PArray<Phyre::PRendering::PSceneRenderPass,4>  size=None  descriptor=0xca6360
struct PSceneRenderPass_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PRendering::PSceneRenderPass *,4>  size=None  descriptor=0xca6240
struct PSceneRenderPass___4_ {
  unsigned int m_count;
};

// Phyre::PPostProcessing::PScreenSpaceReflectionBase  size=148  descriptor=0x19446d8
struct PScreenSpaceReflectionBase {
  unsigned __int8 _pad_0[20];
  float m_minReflectionDirZ;
  float m_marchStepFactor;
  unsigned __int8 _pad_1c[120];
};

// Phyre::PScripting::PScript  size=40  descriptor=0xcbe410
struct PScript {
  unsigned __int8 _pad_0[16];
  PString m_onLoadEntryPoint;
  PString m_onUnloadEntryPoint;
  PString m_defaultEntryPoint;
  PString m_sourceName;
  bool m_isPersistent;
  unsigned __int8 _pad_21[3];
  unsigned int m_executionInterval;
};

// Phyre::PGameplay::PScriptableComponent  size=32  descriptor=0xcaf780
struct PScriptableComponent {
  unsigned __int8 _pad_0[16];
  PString m_entryPoint;
  unsigned __int8 m_script[12];  // Phyre::PScripting::PScript
};

// Phyre::PGameplay::PScriptedComponent  size=48  descriptor=0xcaf8d0
struct PScriptedComponent {
  unsigned __int8 _pad_0[32];
  PString m_onLoadEntryPoint;
  PString m_onUnloadEntryPoint;
  unsigned int m_executionInterval;
  unsigned __int8 _pad_2c[2];
  bool m_suspendOnInitialization;
  unsigned __int8 _pad_2f[1];
};

// Phyre::PRendering::PShader  size=32  descriptor=0xca2548
struct PShader {
  unsigned int m_parameterBufferFrequenciesRequired;
  unsigned __int8 _pad_4[24];
  unsigned int m_parameterBufferSize;
};

// Phyre::PRendering::PShaderParameterCaptureBufferByteAddressBuffer  size=20  descriptor=0xc9c288
struct PShaderParameterCaptureBufferByteAddressBuffer {
  unsigned __int8 _pad_0[12];
  PStructuredBuffer * m_structuredBuffer;
  PDataBlock * m_dataBlock;
};

// Phyre::PRendering::PShaderParameterCaptureBufferDataBlock  size=16  descriptor=0xc9c3b8
struct PShaderParameterCaptureBufferDataBlock {
  unsigned __int8 _pad_0[12];
  PDataBlock * m_dataBlock;
};

// Phyre::PRendering::PShaderParameterCaptureBufferIndexDataBlock  size=16  descriptor=0xc9c450
struct PShaderParameterCaptureBufferIndexDataBlock {
  unsigned __int8 _pad_0[12];
  PIndexDataBlock * m_indexDataBlock;
};

// Phyre::PRendering::PShaderParameterCaptureBufferLocation  size=2  descriptor=0xc9b720
struct PShaderParameterCaptureBufferLocation {
  unsigned __int16 m_offset;
};

// Phyre::PRendering::PShaderParameterCaptureBufferLocationSize  size=4  descriptor=0xc9b7b8
struct PShaderParameterCaptureBufferLocationSize {
  unsigned __int8 _pad_0[2];
  unsigned __int16 m_size;
};

// Phyre::PRendering::PShaderParameterCaptureBufferLocationType  size=4  descriptor=0xc9b850
struct PShaderParameterCaptureBufferLocationType {
  unsigned __int8 _pad_0[2];
  unsigned __int8 m_type;
  unsigned __int8 _pad_3[1];
};

// Phyre::PRendering::PShaderParameterCaptureBufferLocationTypeConstantBuffer  size=16  descriptor=0xc9b8e8
struct PShaderParameterCaptureBufferLocationTypeConstantBuffer {
  unsigned __int8 _pad_0[4];
  unsigned int m_constantBufferLocation;
  unsigned int m_size;
  unsigned __int8 m_type;
  unsigned __int8 _pad_d[3];
};

// Phyre::PArray<Phyre::PRendering::PShaderParameterCaptureBufferLocationTypeConstantBuffer,4>  size=None  descriptor=0xca19e0
struct PShaderParameterCaptureBufferLocationTypeConstantBuffer_4_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PRendering::PShaderParameterCaptureBufferLocationType,2>  size=None  descriptor=0xca18c0
struct PShaderParameterCaptureBufferLocationType_2_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PRendering::PShaderParameterCaptureBufferLocation,2>  size=None  descriptor=0xca1b00
struct PShaderParameterCaptureBufferLocation_2_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShaderParameterCaptureBufferRWByteAddressBuffer  size=24  descriptor=0xc9c320
struct PShaderParameterCaptureBufferRWByteAddressBuffer {
  unsigned __int8 _pad_0[12];
  PStructuredBuffer * m_rwStructuredBuffer;
  PDataBlock * m_rwDataBlock;
  PIndirectArgsBuffer * m_rwIndirectArgsBuffer;
};

// Phyre::PRendering::PShaderParameterCaptureBufferRWStructuredBuffer  size=20  descriptor=0xc9c1f0
struct PShaderParameterCaptureBufferRWStructuredBuffer {
  unsigned __int8 _pad_0[12];
  PStructuredBuffer * m_rwStructuredBuffer;
  PDataBlock * m_rwDataBlock;
};

// Phyre::PRendering::PShaderParameterCaptureBufferRWTexture2D  size=16  descriptor=0xc9c4e8
struct PShaderParameterCaptureBufferRWTexture2D {
  unsigned __int8 _pad_0[12];
  PTexture2D * m_texture;
};

// Phyre::PRendering::PShaderParameterCaptureBufferRWTexture3D  size=16  descriptor=0xc9c580
struct PShaderParameterCaptureBufferRWTexture3D {
  unsigned __int8 _pad_0[12];
  PTexture3D * m_texture;
};

// Phyre::PRendering::PShaderParameterCaptureBufferSampler  size=16  descriptor=0xc9c0c0
struct PShaderParameterCaptureBufferSampler {
  unsigned __int8 _pad_0[12];
  PTextureCommonBase * m_unusedPointer;
};

// Phyre::PRendering::PShaderParameterCaptureBufferStructuredBuffer  size=20  descriptor=0xc9c158
struct PShaderParameterCaptureBufferStructuredBuffer {
  unsigned __int8 _pad_0[12];
  PStructuredBuffer * m_structuredBuffer;
  PDataBlock * m_dataBlock;
};

// Phyre::PRendering::PShaderParameterCaptureBufferTexture2D  size=16  descriptor=0xc9bef8
struct PShaderParameterCaptureBufferTexture2D {
  unsigned __int8 _pad_0[12];
  PTexture2D * m_texture;
};

// Phyre::PRendering::PShaderParameterCaptureBufferTexture3D  size=16  descriptor=0xc9bf90
struct PShaderParameterCaptureBufferTexture3D {
  unsigned __int8 _pad_0[12];
  PTexture3D * m_texture;
};

// Phyre::PRendering::PShaderParameterCaptureBufferTextureBase  size=12  descriptor=0xc9bdc8
struct PShaderParameterCaptureBufferTextureBase {
  unsigned int m_parameterType;
  unsigned int m_textureBufferIndex;
  PSamplerState * m_samplerState;
};

// Phyre::PRendering::PShaderParameterCaptureBufferTextureCubeMap  size=16  descriptor=0xc9c028
struct PShaderParameterCaptureBufferTextureCubeMap {
  unsigned __int8 _pad_0[12];
  PTextureCubeMap * m_texture;
};

// Phyre::PRendering::PShaderParameterCaptureConstantBuffer  size=16  descriptor=0xc9be60
struct PShaderParameterCaptureConstantBuffer {
  unsigned __int8 _pad_0[12];
  PConstantBuffer * m_constantBuffer;
};

// Phyre::PRendering::PShaderParameterDefinition  size=16  descriptor=0xc9b980
struct PShaderParameterDefinition {
  unsigned __int16 m_arrayElementCount;
  unsigned __int8 m_parameterType;
  unsigned __int8 m_dataType;
  PString m_name;
  PShaderParameterCaptureBufferLocationSize m_bufferLoc;
  unsigned int m_constantBufferLocation;
};

// Phyre::PArray<Phyre::PRendering::PShaderParameterDefinition,4>  size=None  descriptor=0xc9d0e0
struct PShaderParameterDefinition_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShaderPassBase  size=24  descriptor=0xca1500
struct PShaderPassBase {
  PShaderVertexProgram * m_vertexProgram;
  PShaderFragmentProgram * m_fragmentProgram;
  PShaderGeometryProgram * m_geometryProgram;
  unsigned __int8 m_computeProgram[12];  // Phyre::PRendering::PShaderComputeProgram
};

// Phyre::PRendering::PShaderPassStateD3D11  size=380  descriptor=0xcb34e0
struct PShaderPassStateD3D11 {
  unsigned __int8 _pad_0[4];
  CD3D11_RASTERIZER_DESC m_rasterDesc;
  CD3D11_DEPTH_STENCIL_DESC m_depthDesc;
  CD3D11_BLEND_DESC m_blendDesc;
  unsigned __int8 _pad_168[16];
  unsigned __int8 m_stencilRef;
  unsigned __int8 _pad_179[3];
};

// Phyre::PRendering::PShaderPassD3D11  size=596  descriptor=0xcb3448
struct PShaderPassD3D11 {
  unsigned __int8 _pad_0[24];
  PShaderPassStateD3D11 m_state;
  PShaderPassParameterLocationTypesConstantBuffer m_vertexParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_fragmentParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_geometryParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_computeParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_vertexTexParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_fragmentTexParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_geometryTexParameterLocation;
  PShaderPassParameterLocationTypesConstantBuffer m_computeTexParameterLocation;
};

// Phyre::PRendering::PShaderPassInfo  size=32  descriptor=0xca55c8
struct PShaderPassInfo {
  PString m_vertexEntryPoint;
  PString m_fragmentEntryPoint;
  PString m_geometryEntryPoint;
  PString m_computeEntryPoint;
  unsigned int m_vertexProfile;
  unsigned int m_fragmentProfile;
  unsigned int m_geometryProfile;
  unsigned int m_computeProfile;
};

// Phyre::PArray<Phyre::PRendering::PShaderPassInfo,4>  size=None  descriptor=0xca5bc0
struct PShaderPassInfo_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShaderPassStateBase  size=4  descriptor=0xca1630
struct PShaderPassStateBase {
  unsigned int m_importantState;
};

// Phyre::PArray<Phyre::PRendering::PShaderPass,4>  size=None  descriptor=0xca26d8
struct PShaderPass_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShaderProgramD3D11  size=1248  descriptor=0xcb36a8
struct PShaderProgramD3D11 {
  unsigned __int8 _pad_0[20];
  unsigned int m_constantBufferSize;
  unsigned int m_globalConstantBufferIndex;
  unsigned __int8 _pad_1c[1216];
  unsigned int m_shaderProfile;
};

// Phyre::PRendering::PShaderSource  size=20  descriptor=0xc9c618
struct PShaderSource {
  PString m_code;
  PString m_entry;
  unsigned __int8 _pad_8[8];
  unsigned int m_profile;
};

// Phyre::PRendering::PShaderStreamDefinition  size=16  descriptor=0xc9c7e0
struct PShaderStreamDefinition {
  unsigned __int8 m_renderType[4];  // PRenderDataType
  PString m_name;
  PShaderParameterCaptureBufferLocationSize m_bufferLoc;
  unsigned __int16 m_nameHash;
  unsigned __int8 m_dataType;
  unsigned __int8 m_index;
};

// Phyre::PArray<Phyre::PRendering::PShaderStreamDefinition,4>  size=None  descriptor=0xc9d200
struct PShaderStreamDefinition_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShaderVertexProgramD3D11  size=1264  descriptor=0xcb3740
struct PShaderVertexProgramD3D11 {
  unsigned __int8 _pad_0[1248];
  PStreamInputLayoutD3D11 m_inputLayout;
  unsigned __int8 _pad_4ec[4];
};

// Phyre::PArray<Phyre::PRendering::PShader,4>  size=None  descriptor=0xca5ce0
struct PShader_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShadowCaster  size=104  descriptor=0xca44c8
struct PShadowCaster {
  PLight * m_light;
  unsigned __int8 m_shadowCasterType[60];  // ?
  float m_zbias;
  unsigned __int8 _pad_44[36];
};

// Phyre::PArray<Phyre::PRendering::PShadowCasterType const *,4>  size=None  descriptor=0xca6bd0
struct PShadowCasterType_const___4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PShadowCaster::PShadowSplit  size=224  descriptor=0xca4430
struct PShadowSplit {
  unsigned __int8 _pad_0[196];
  float m_farPlane;
  unsigned __int8 _pad_c8[24];
};

// Phyre::PArray<Phyre::PRendering::PShadowCaster::PShadowSplit,4>  size=None  descriptor=0xca4680
struct PShadowSplit_4_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PShape  size=28  descriptor=0xc9ab98
struct PShape {
  unsigned int m_indexCount;
  unsigned __int8 _pad_4[8];
  unsigned int m_vertexCount;
  unsigned __int8 _pad_10[8];
  unsigned __int8 m_vertexFormat;
  unsigned __int8 m_indexFormat;
  unsigned __int8 _pad_1a[2];
};

// Phyre::PSharray<unsigned int>  size=None  descriptor=0xca4c98
struct PSharray_unsigned_int_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PSkeletonJointBounds  size=32  descriptor=0xc99be8
struct PSkeletonJointBounds {
  unsigned __int8 _pad_0[12];
  unsigned int m_hierarchyMatrixIndex;
  unsigned __int8 _pad_10[12];
  unsigned int m_pad;
};

// Phyre::PArray<Phyre::PGeometry::PSkeletonJointBounds,4>  size=None  descriptor=0xc9a2d0
struct PSkeletonJointBounds_4_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PSkinBoneRemap  size=4  descriptor=0xc99200
struct PSkinBoneRemap {
  unsigned __int16 m_hierarchyMatrixIndex;
  unsigned __int16 m_skeletonMatrixIndex;
};

// Phyre::PArray<Phyre::PGeometry::PSkinBoneRemap,2>  size=None  descriptor=0xc99590
struct PSkinBoneRemap_2_ {
  unsigned int m_count;
};

// Phyre::PGameplay::PSpline  size=44  descriptor=0xcae7b8
struct PSpline {
  unsigned __int8 _pad_0[20];
  PWorldMatrix * m_localToWorldMatrix;
  bool m_isLoop;
  unsigned __int8 _pad_19[3];
  float m_tension;
  unsigned __int8 _pad_20[12];
};

// Phyre::PGameplay::PSplineFollowerComponent  size=28  descriptor=0xcafd60
struct PSplineFollowerComponent {
  unsigned __int8 _pad_0[12];
  PSpline * m_spline;
  float m_speed;
  float m_distanceTravelled;
  bool m_autoUpdate;
  unsigned __int8 _pad_19[3];
};

// Phyre::PSprite::PSpriteAnimationInfo  size=20  descriptor=0xcb1eb8
struct PSpriteAnimationInfo {
  PString m_name;
  PTextureAtlasInfo * m_textureAtlasInfo;
  float m_timeInterval;
  unsigned __int8 _pad_c[8];
};

// Phyre::PSprite::PSpriteAnimationInfoChar  size=40  descriptor=0xcb1f50
struct PSpriteAnimationInfoChar {
  unsigned __int8 _pad_0[20];
  PString m_groupName;
  float mUStart;
  float mVStart;
  float mUEnd;
  float mVEnd;
};

// Phyre::PSprite::PSpriteAnimationInfoInstance  size=40  descriptor=0xcb3158
struct PSpriteAnimationInfoInstance {
  PWorldMatrix * m_localToWorldMatrix;
  PSpriteCollection * m_spriteCollection;
  PSpriteAnimationInfo m_spriteAnimInfo;
  unsigned __int8 _pad_1c[4];
  bool m_flipX;
  bool m_flipY;
  unsigned __int8 _pad_22[6];
};

// Phyre::PSprite::PSpriteAttributes  size=48  descriptor=0xcb1c58
struct PSpriteAttributes {
  float m_posX;
  float m_posY;
  float m_sinPhi;
  float m_cosPhi;
  float m_uOrigin;
  float m_vOrigin;
  float m_textureSizeX;
  float m_textureSizeY;
  float m_spriteSizeX;
  float m_spriteSizeY;
  float m_depth;
  float m_phi;
};

// Phyre::PSprite::PSpriteCollection  size=220  descriptor=0xcb1cf0
struct PSpriteCollection {
  unsigned __int8 _pad_0[4];
  bool m_sort;
  unsigned __int8 _pad_5[7];
  unsigned __int8 m_mesh[8];  // Phyre::PGeometry::PMesh
  PMaterialSet m_quadMaterialSet;
  unsigned int m_maxSpriteCount;
  unsigned int m_currentSpriteCount;
  PMeshSegment m_instanceSegment;
  unsigned __int8 m_textureAtlas[40];  // Phyre::PRendering::PTexture2D
  PSpriteAttributes * m_sprites;
  PMaterial * m_material;
  unsigned int m_nextAvailableSpriteID;
  unsigned __int8 _pad_c4[24];
};

// Phyre::PRendering::PStreamInputDescD3D11  size=20  descriptor=0xcb3578
struct PStreamInputDescD3D11 {
  PString m_semantic;
  unsigned __int8 m_renderType[4];  // PRenderDataType
  unsigned int m_semanticIndex;
  unsigned int m_d3dFormat;
  unsigned int m_inputSlot;
};

// Phyre::PArray<Phyre::PRendering::PStreamInputDescD3D11,4>  size=None  descriptor=0xcb4c58
struct PStreamInputDescD3D11_4_ {
  unsigned int m_count;
};

// Phyre::PSharray<Phyre::PString>  size=None  descriptor=0xc91a28
struct PString_ {
  unsigned int m_count;
};

// Phyre::PArray<Phyre::PString,4>  size=None  descriptor=0xc9a090
struct PString_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PStructuredBufferBase  size=20  descriptor=0xca8d30
struct PStructuredBufferBase {
  unsigned int m_elementCount;
  unsigned int m_elementSize;
  unsigned int m_flags;
  unsigned __int8 _pad_c[8];
};

// Phyre::PSprite::PSubTextureInfo  size=20  descriptor=0xcb1d88
struct PSubTextureInfo {
  PString m_name;
  float m_uOrigin;
  float m_vOrigin;
  float m_textureSizeX;
  float m_textureSizeY;
};

// Phyre::PArray<Phyre::PSprite::PSubTextureInfo,4>  size=None  descriptor=0xcb2a60
struct PSubTextureInfo_4_ {
  unsigned int m_count;
};

// Phyre::PRendering::PTexture2DBase  size=36  descriptor=0xca3738
struct PTexture2DBase {
  unsigned __int8 _pad_0[28];
  unsigned int m_width;
  unsigned int m_height;
};

// Phyre::PRendering::PTexture3DBase  size=40  descriptor=0xca3ac8
struct PTexture3DBase {
  unsigned __int8 _pad_0[28];
  unsigned int m_width;
  unsigned int m_height;
  unsigned int m_depth;
};

// Phyre::PSprite::PTextureAtlasInfo  size=12  descriptor=0xcb1e20
struct PTextureAtlasInfo {
  unsigned __int8 m_texture[12];  // Phyre::PRendering::PTexture2D
};

// Phyre::PRendering::PTextureCommonBase  size=28  descriptor=0xca3530
struct PTextureCommonBase {
  unsigned __int8 m_format[5];  // PTextureFormatBase
  unsigned __int8 m_memoryType;
  unsigned __int8 _pad_6[6];
  unsigned int m_mipmapCount;
  unsigned int m_maxMipLevel;
  int m_textureFlags;
  unsigned __int8 _pad_18[4];
};

// Phyre::PRendering::PTextureCubeMapBase  size=32  descriptor=0xca3e58
struct PTextureCubeMapBase {
  unsigned __int8 _pad_0[28];
  unsigned int m_size;
};

// Phyre::PGameplay::PTimerComponent  size=44  descriptor=0xcafb70
struct PTimerComponent {
  unsigned __int8 _pad_0[32];
  float m_timeOut;
  unsigned __int8 _pad_24[8];
};

// Phyre::PGameplay::PTrigger  size=68  descriptor=0xcaed58
struct PTrigger {
  unsigned __int8 m_type[12];  // PTriggerType
  PWorldMatrix * m_localToWorldMatrix;
  PWorldMatrix m_previousLocalToWorldMatrix;
  bool m_enabled;
  unsigned __int8 _pad_41[3];
};

// Phyre::PGameplay::PTriggerReceiverComponent  size=44  descriptor=0xcaf388
struct PTriggerReceiverComponent {
  unsigned __int8 _pad_0[12];
  unsigned __int8 m_script[8];  // Phyre::PScripting::PScript
  PScriptCallbackHandler m_entryHandler;
  PScriptCallbackHandler m_exitHandler;
};

// Phyre::PSharray<Phyre::PGameplay::PTriggerReceiverComponent *>  size=None  descriptor=0xcaef78
struct PTriggerReceiverComponent___ {
  unsigned int m_count;
};

// Phyre::PGameplay::PTriggerReceiverTypeCallbackData  size=20  descriptor=0xcaf1e8
struct PTriggerReceiverTypeCallbackData {
  PTypedObject m_triggerReceiverComponent;
  PTrigger * m_trigger;
  PQuarryComponent * m_quarryComponent;
  unsigned int m_entryCount;
};

// Phyre::PArray<Phyre::PTypedObject,4>  size=None  descriptor=0xc93c58
struct PTypedObject_4_ {
  unsigned int m_count;
};

// Phyre::PGeometry::PVertexStream  size=12  descriptor=0xc98730
struct PVertexStream {
  unsigned int m_offset;
  unsigned __int8 m_renderDataType[4];  // PRenderDataType
  unsigned __int8 m_type;
  unsigned __int8 m_streamSet;
  unsigned __int8 _pad_a[2];
};

// Phyre::PArray<Phyre::PGeometry::PVertexStream,4>  size=None  descriptor=0xc98a98
struct PVertexStream_4_ {
  unsigned int m_count;
};

// Phyre::PArray<sce::Vectormath::Scalar::Aos::Vector3,4>  size=None  descriptor=0xcaeaa8
struct Vector3_4_ {
  unsigned int m_count;
};
