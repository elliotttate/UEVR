;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD                 0   xy          0     NONE   float   xy  
; SV_Position              0   xyzw        1      POS   float       
; SV_RenderTargetArrayIndex     0   x           2  RTINDEX    uint   x   
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
; SV_Target                1   xyzw        1   TARGET   float   xyzw
;
; shader debug name: 13c87d0d96adc433d829c1eb2f216419.pdb
; shader hash: 13c87d0d96adc433d829c1eb2f216419
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Pixel Shader
; DepthOutput=0
; SampleFrequency=0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 3
; SigOutputElements: 2
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 3
; SigOutputVectors[0]: 2
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: InjectMainPS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD                 0          noperspective       
; SV_Position              0          noperspective       
; SV_RenderTargetArrayIndex     0        nointerpolation       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
; SV_Target                1                              
;
; Buffer Definitions:
;
; cbuffer 
; {
;
;   [340 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [10076 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [184 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [308 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [156 x i8] (type annotation not present)
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb0     1
;                                   cbuffer      NA          NA     CB1            cb1     1
;                                   cbuffer      NA          NA     CB2            cb2     1
;                                   cbuffer      NA          NA     CB3            cb3     1
;                                   cbuffer      NA          NA     CB4            cb4     1
;                                   sampler      NA          NA      S0             s0     1
;                                   sampler      NA          NA      S1             s1     1
;                                   sampler      NA          NA      S2             s2     1
;                                   sampler      NA          NA      S3             s3     1
;                                   sampler      NA          NA      S4             s4     1
;                                   texture     f32          2d      T0             t0     1
;                                   texture    byte         r/o      T1             t1     1
;                                   texture     u32          2d      T2             t2     1
;                                   texture     u32     2darray      T3             t3     1
;                                   texture     f32          2d      T4             t4     1
;                                   texture     f32          2d      T5             t5     1
;                                   texture     f32          2d      T6             t6     1
;                                   texture     f32          2d      T7             t7     1
;
;
; ViewId state:
;
; Number of inputs: 9, outputs: 8
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1, 8 }
;   output 1 depends on inputs: { 0, 1, 8 }
;   output 2 depends on inputs: { 0, 1, 8 }
;   output 3 depends on inputs: { 0, 1, 8 }
;   output 4 depends on inputs: { 0, 1, 8 }
;   output 5 depends on inputs: { 0, 1, 8 }
;   output 6 depends on inputs: { 0, 1, 8 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%struct.ByteAddressBuffer = type { i32 }
%"class.Texture2D<unsigned int>" = type { i32, %"class.Texture2D<unsigned int>::mips_type" }
%"class.Texture2D<unsigned int>::mips_type" = type { i32 }
%"class.Texture2DArray<unsigned int>" = type { i32, %"class.Texture2DArray<unsigned int>::mips_type" }
%"class.Texture2DArray<unsigned int>::mips_type" = type { i32 }
%"class.Texture2D<vector<float, 3> >" = type { <3 x float>, %"class.Texture2D<vector<float, 3> >::mips_type" }
%"class.Texture2D<vector<float, 3> >::mips_type" = type { i32 }
%"hostlayout.$Globals" = type { <4 x float>, <4 x float>, i32, [4 x <4 x float>], <4 x float>, i32, float, [4 x <4 x float>], float, i32, i32, <4 x float>, [4 x <4 x float>], [2 x <4 x float>], i32 }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%DeferredLightUniforms = type { <4 x float>, <2 x float>, float, float, float, float, i32, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <2 x float>, float, float, float, float, <2 x float>, <2 x float>, float, float, i32, i32 }
%VirtualShadowMap = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, <2 x i32>, <2 x i32>, i32, i32, i32, i32, <4 x float>, i32, float, float, float, i32, float, i32, i32, float, float, i32, i32, i32, float, float, float, float, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32 }
%Material = type { [6 x <4 x float>], i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @InjectMainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 7, i32 7, i32 0, i8 0 }, i32 7, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 6, i32 6, i32 0, i8 0 }, i32 6, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 5, i32 5, i32 0, i8 0 }, i32 5, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 0 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %9 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 3 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %10 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 3 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %11 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 3 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %12 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 3 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %13 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %14 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 2 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %15 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 2 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %16 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %17 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %18 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %19 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %14, %dx.types.ResourceProperties { i32 13, i32 156 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %20 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %15, %dx.types.ResourceProperties { i32 13, i32 308 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %21 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %16, %dx.types.ResourceProperties { i32 13, i32 184 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %22 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %17, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %23 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %18, %dx.types.ResourceProperties { i32 13, i32 340 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %24 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %25 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !42  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %26 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !42  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %27 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 121)  ; CBufferLoadLegacy(handle,regIndex)
  %28 = extractvalue %dx.types.CBufRet.f32 %27, 0
  %29 = extractvalue %dx.types.CBufRet.f32 %27, 1
  %30 = extractvalue %dx.types.CBufRet.f32 %27, 2
  %31 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 124)  ; CBufferLoadLegacy(handle,regIndex)
  %32 = extractvalue %dx.types.CBufRet.f32 %31, 0
  %33 = extractvalue %dx.types.CBufRet.f32 %31, 1
  %34 = extractvalue %dx.types.CBufRet.f32 %31, 2
  %35 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 159)  ; CBufferLoadLegacy(handle,regIndex)
  %36 = extractvalue %dx.types.CBufRet.f32 %35, 0
  %37 = extractvalue %dx.types.CBufRet.f32 %35, 1
  %38 = extractvalue %dx.types.CBufRet.f32 %35, 2
  %39 = extractvalue %dx.types.CBufRet.f32 %35, 3
  %40 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 182)  ; CBufferLoadLegacy(handle,regIndex)
  %41 = extractvalue %dx.types.CBufRet.f32 %40, 0
  %42 = extractvalue %dx.types.CBufRet.f32 %40, 1
  %43 = extractvalue %dx.types.CBufRet.f32 %40, 2
  %44 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %45 = extractvalue %dx.types.CBufRet.i32 %44, 0
  %46 = add i32 %45, 170
  %47 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 %46)  ; CBufferLoadLegacy(handle,regIndex)
  %48 = extractvalue %dx.types.CBufRet.f32 %47, 2
  %49 = uitofp i32 %24 to float
  %50 = fadd float %49, 5.000000e-01
  %51 = add i32 %45, 172
  %52 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 %51)  ; CBufferLoadLegacy(handle,regIndex)
  %53 = extractvalue %dx.types.CBufRet.f32 %52, 3
  %54 = fmul float %50, %53
  %55 = fadd float %48, %54
  %56 = extractvalue %dx.types.CBufRet.f32 %47, 0
  %57 = extractvalue %dx.types.CBufRet.f32 %47, 1
  %58 = extractvalue %dx.types.CBufRet.f32 %52, 0
  %59 = extractvalue %dx.types.CBufRet.f32 %52, 1
  %60 = fdiv float %25, %58
  %61 = fdiv float %26, %59
  %62 = fadd float %56, %60
  %63 = fadd float %57, %61
  %64 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %65 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 9)  ; CBufferLoadLegacy(handle,regIndex)
  %66 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %67 = extractvalue %dx.types.CBufRet.f32 %66, 0
  %68 = extractvalue %dx.types.CBufRet.f32 %66, 1
  %69 = extractvalue %dx.types.CBufRet.f32 %66, 2
  %70 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %71 = extractvalue %dx.types.CBufRet.f32 %70, 0
  %72 = extractvalue %dx.types.CBufRet.f32 %70, 1
  %73 = extractvalue %dx.types.CBufRet.f32 %70, 2
  %74 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 19)  ; CBufferLoadLegacy(handle,regIndex)
  %75 = extractvalue %dx.types.CBufRet.f32 %74, 0
  %76 = extractvalue %dx.types.CBufRet.f32 %74, 1
  %77 = extractvalue %dx.types.CBufRet.f32 %74, 2
  %78 = extractvalue %dx.types.CBufRet.f32 %74, 3
  %79 = call float @dx.op.dot4.f32(i32 56, float %75, float %76, float %77, float %78, float %62, float %63, float %55, float 1.000000e+00)  ; Dot4(ax,ay,az,aw,bx,by,bz,bw)
  %80 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 20)  ; CBufferLoadLegacy(handle,regIndex)
  %81 = extractvalue %dx.types.CBufRet.f32 %80, 0
  %82 = extractvalue %dx.types.CBufRet.f32 %80, 1
  %83 = extractvalue %dx.types.CBufRet.f32 %80, 2
  %84 = extractvalue %dx.types.CBufRet.f32 %80, 3
  %85 = call float @dx.op.dot4.f32(i32 56, float %81, float %82, float %83, float %84, float %62, float %63, float %55, float 1.000000e+00)  ; Dot4(ax,ay,az,aw,bx,by,bz,bw)
  %86 = fcmp ogt float %79, -0.000000e+00
  call void @dx.op.discard(i32 82, i1 %86)  ; Discard(condition)
  %87 = fcmp ogt float %85, -0.000000e+00
  call void @dx.op.discard(i32 82, i1 %87)  ; Discard(condition)
  %88 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %89 = extractvalue %dx.types.CBufRet.f32 %88, 0
  %90 = fmul fast float %79, %89
  %91 = fsub fast float -0.000000e+00, %90
  %92 = call float @dx.op.unary.f32(i32 7, float %91)  ; Saturate(value)
  %93 = extractvalue %dx.types.CBufRet.f32 %88, 1
  %94 = fmul fast float %85, %93
  %95 = fsub fast float -0.000000e+00, %94
  %96 = call float @dx.op.unary.f32(i32 7, float %95)  ; Saturate(value)
  %97 = fmul fast float %96, %92
  %98 = call float @dx.op.dot3.f32(i32 55, float %71, float %72, float %73, float %71, float %72, float %73), !dx.precise !42  ; Dot3(ax,ay,az,bx,by,bz)
  %99 = call float @dx.op.unary.f32(i32 25, float %98), !dx.precise !42  ; Rsqrt(value)
  %100 = fmul float %71, %99
  %101 = fmul float %72, %99
  %102 = fmul float %73, %99
  %103 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %104 = extractvalue %dx.types.CBufRet.i32 %103, 0
  %105 = add i32 %104, 172
  %106 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 %105)  ; CBufferLoadLegacy(handle,regIndex)
  %107 = extractvalue %dx.types.CBufRet.f32 %106, 3
  %108 = fmul float %100, -5.000000e-01
  %109 = fmul float %101, -5.000000e-01
  %110 = fmul float %102, -5.000000e-01
  %111 = fmul float %108, %107
  %112 = fmul float %109, %107
  %113 = fmul float %110, %107
  %114 = fadd float %62, %111
  %115 = fadd float %63, %112
  %116 = fadd float %55, %113
  %117 = fsub fast float -0.000000e+00, %116
  %118 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %119 = extractvalue %dx.types.CBufRet.i32 %118, 0
  %120 = icmp eq i32 %119, 0
  br i1 %120, label %284, label %121, !dx.controlflow.hints !44

; <label>:121                                     ; preds = %0
  %122 = extractvalue %dx.types.CBufRet.f32 %64, 0
  %123 = fcmp fast ogt float %122, -2.000000e+00
  %124 = extractvalue %dx.types.CBufRet.f32 %65, 1
  %125 = fcmp ule float %124, -2.000000e+00
  %126 = and i1 %123, %125
  br i1 %126, label %174, label %127, !dx.controlflow.hints !45

; <label>:127                                     ; preds = %121
  %128 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %21, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %129 = extractvalue %dx.types.CBufRet.f32 %128, 0
  %130 = extractvalue %dx.types.CBufRet.f32 %128, 1
  %131 = extractvalue %dx.types.CBufRet.f32 %128, 2
  %132 = fsub fast float %114, %129
  %133 = fsub fast float %115, %130
  %134 = fsub fast float %116, %131
  %135 = fmul fast float %132, %132
  %136 = fmul fast float %133, %133
  %137 = fadd fast float %135, %136
  %138 = fmul fast float %134, %134
  %139 = fadd fast float %137, %138
  %140 = call float @dx.op.unary.f32(i32 24, float %139)  ; Sqrt(value)
  %141 = fdiv fast float %132, %140
  %142 = fdiv fast float %133, %140
  %143 = fdiv fast float %134, %140
  %144 = fdiv fast float %142, %141
  %145 = call float @dx.op.unary.f32(i32 17, float %144)  ; Atan(value)
  %146 = fadd fast float %145, 0x400921FB60000000
  %147 = fadd fast float %145, 0xC00921FB60000000
  %148 = fcmp fast olt float %141, 0.000000e+00
  %149 = fcmp fast oeq float %141, 0.000000e+00
  %150 = fcmp fast oge float %142, 0.000000e+00
  %151 = fcmp fast olt float %142, 0.000000e+00
  %152 = and i1 %148, %150
  %153 = select i1 %152, float %146, float %145
  %154 = and i1 %148, %151
  %155 = select i1 %154, float %147, float %153
  %156 = and i1 %149, %151
  %157 = and i1 %149, %150
  %158 = fmul fast float %155, 0x3FC45F3060000000
  %159 = select i1 %156, float -2.500000e-01, float %158
  %160 = select i1 %157, float 2.500000e-01, float %159
  %161 = fcmp fast ogt float %160, 0.000000e+00
  %162 = fadd fast float %160, 1.000000e+00
  %163 = select i1 %161, float %160, float %162
  %164 = call float @dx.op.unary.f32(i32 15, float %143)  ; Acos(value)
  %165 = fmul fast float %164, 0x3FD45F3060000000
  %166 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %167 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %10, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %168 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %166, %dx.types.Handle %167, float %163, float %165, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %169 = extractvalue %dx.types.ResRet.f32 %168, 0
  %170 = extractvalue %dx.types.CBufRet.f32 %128, 3
  %171 = fmul fast float %170, %140
  %172 = fcmp fast olt float %171, %169
  %173 = uitofp i1 %172 to float
  br label %284

; <label>:174                                     ; preds = %121
  %175 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %176 = extractvalue %dx.types.CBufRet.f32 %175, 0
  %177 = extractvalue %dx.types.CBufRet.f32 %175, 1
  %178 = extractvalue %dx.types.CBufRet.f32 %175, 3
  %179 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %180 = extractvalue %dx.types.CBufRet.f32 %179, 0
  %181 = extractvalue %dx.types.CBufRet.f32 %179, 1
  %182 = extractvalue %dx.types.CBufRet.f32 %179, 3
  %183 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %184 = extractvalue %dx.types.CBufRet.f32 %183, 0
  %185 = extractvalue %dx.types.CBufRet.f32 %183, 1
  %186 = extractvalue %dx.types.CBufRet.f32 %183, 3
  %187 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 6)  ; CBufferLoadLegacy(handle,regIndex)
  %188 = extractvalue %dx.types.CBufRet.f32 %187, 0
  %189 = extractvalue %dx.types.CBufRet.f32 %187, 1
  %190 = extractvalue %dx.types.CBufRet.f32 %187, 3
  %191 = fmul fast float %176, %114
  %192 = call float @dx.op.tertiary.f32(i32 46, float %115, float %180, float %191)  ; FMad(a,b,c)
  %193 = call float @dx.op.tertiary.f32(i32 46, float %116, float %184, float %192)  ; FMad(a,b,c)
  %194 = fadd fast float %193, %188
  %195 = fmul fast float %177, %114
  %196 = call float @dx.op.tertiary.f32(i32 46, float %115, float %181, float %195)  ; FMad(a,b,c)
  %197 = call float @dx.op.tertiary.f32(i32 46, float %116, float %185, float %196)  ; FMad(a,b,c)
  %198 = fadd fast float %197, %189
  %199 = fmul fast float %178, %114
  %200 = call float @dx.op.tertiary.f32(i32 46, float %115, float %182, float %199)  ; FMad(a,b,c)
  %201 = call float @dx.op.tertiary.f32(i32 46, float %116, float %186, float %200)  ; FMad(a,b,c)
  %202 = fadd fast float %201, %190
  %203 = fdiv fast float %194, %202
  %204 = fdiv fast float %198, %202
  %205 = fcmp fast ole float %203, 1.000000e+00
  %206 = fcmp fast ole float %204, 1.000000e+00
  %207 = fcmp fast oge float %203, 0.000000e+00
  %208 = fcmp fast oge float %204, 0.000000e+00
  %209 = and i1 %207, %205
  %210 = and i1 %208, %206
  %211 = and i1 %209, %210
  br i1 %211, label %212, label %284

; <label>:212                                     ; preds = %174
  %213 = extractvalue %dx.types.CBufRet.f32 %187, 2
  %214 = extractvalue %dx.types.CBufRet.f32 %183, 2
  %215 = extractvalue %dx.types.CBufRet.f32 %179, 2
  %216 = extractvalue %dx.types.CBufRet.f32 %175, 2
  %217 = fmul fast float %216, %114
  %218 = call float @dx.op.tertiary.f32(i32 46, float %115, float %215, float %217)  ; FMad(a,b,c)
  %219 = call float @dx.op.tertiary.f32(i32 46, float %116, float %214, float %218)  ; FMad(a,b,c)
  %220 = fadd fast float %219, %213
  %221 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 7)  ; CBufferLoadLegacy(handle,regIndex)
  %222 = extractvalue %dx.types.CBufRet.f32 %221, 0
  %223 = extractvalue %dx.types.CBufRet.f32 %221, 1
  %224 = extractvalue %dx.types.CBufRet.f32 %221, 2
  %225 = extractvalue %dx.types.CBufRet.f32 %221, 3
  %226 = fmul fast float %222, %203
  %227 = fmul fast float %223, %204
  %228 = fadd fast float %226, -5.000000e-01
  %229 = fadd fast float %227, -5.000000e-01
  %230 = call float @dx.op.unary.f32(i32 22, float %228)  ; Frc(value)
  %231 = call float @dx.op.unary.f32(i32 22, float %229)  ; Frc(value)
  %232 = call float @dx.op.unary.f32(i32 27, float %228)  ; Round_ni(value)
  %233 = call float @dx.op.unary.f32(i32 27, float %229)  ; Round_ni(value)
  %234 = fadd fast float %232, 1.000000e+00
  %235 = fadd fast float %233, 1.000000e+00
  %236 = fmul fast float %234, %224
  %237 = fmul fast float %235, %225
  %238 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %239 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %10, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %240 = call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73, %dx.types.Handle %238, %dx.types.Handle %239, float %236, float %237, float undef, float undef, i32 0, i32 0, i32 0)  ; TextureGather(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,channel)
  %241 = extractvalue %dx.types.ResRet.f32 %240, 0
  %242 = extractvalue %dx.types.ResRet.f32 %240, 1
  %243 = extractvalue %dx.types.ResRet.f32 %240, 2
  %244 = extractvalue %dx.types.ResRet.f32 %240, 3
  %245 = fmul fast float %220, 4.000000e+01
  %246 = fadd fast float %245, -1.000000e+00
  %247 = fmul fast float %241, 4.000000e+01
  %248 = fmul fast float %242, 4.000000e+01
  %249 = fmul fast float %243, 4.000000e+01
  %250 = fmul fast float %244, 4.000000e+01
  %251 = fsub fast float %247, %246
  %252 = fsub fast float %248, %246
  %253 = fsub fast float %249, %246
  %254 = fsub fast float %250, %246
  %255 = call float @dx.op.unary.f32(i32 7, float %251)  ; Saturate(value)
  %256 = call float @dx.op.unary.f32(i32 7, float %252)  ; Saturate(value)
  %257 = call float @dx.op.unary.f32(i32 7, float %253)  ; Saturate(value)
  %258 = call float @dx.op.unary.f32(i32 7, float %254)  ; Saturate(value)
  %259 = fcmp fast ogt float %241, 0x3FEFAE1480000000
  %260 = fcmp fast ogt float %242, 0x3FEFAE1480000000
  %261 = fcmp fast ogt float %243, 0x3FEFAE1480000000
  %262 = fcmp fast ogt float %244, 0x3FEFAE1480000000
  %263 = uitofp i1 %259 to float
  %264 = uitofp i1 %260 to float
  %265 = uitofp i1 %261 to float
  %266 = uitofp i1 %262 to float
  %267 = fadd fast float %263, %255
  %268 = fadd fast float %264, %256
  %269 = fadd fast float %257, %265
  %270 = fadd fast float %258, %266
  %271 = call float @dx.op.unary.f32(i32 7, float %267)  ; Saturate(value)
  %272 = call float @dx.op.unary.f32(i32 7, float %268)  ; Saturate(value)
  %273 = call float @dx.op.unary.f32(i32 7, float %269)  ; Saturate(value)
  %274 = call float @dx.op.unary.f32(i32 7, float %270)  ; Saturate(value)
  %275 = fsub fast float %273, %274
  %276 = fsub fast float %272, %271
  %277 = fmul fast float %275, %230
  %278 = fmul fast float %276, %230
  %279 = fadd fast float %277, %274
  %280 = fadd fast float %278, %271
  %281 = fsub fast float %280, %279
  %282 = fmul fast float %281, %231
  %283 = fadd fast float %282, %279
  br label %284

; <label>:284                                     ; preds = %212, %174, %127, %0
  %285 = phi float [ %173, %127 ], [ %283, %212 ], [ 1.000000e+00, %174 ], [ 1.000000e+00, %0 ]
  %286 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 21)  ; CBufferLoadLegacy(handle,regIndex)
  %287 = extractvalue %dx.types.CBufRet.i32 %286, 0
  %288 = mul i32 %287, 288
  %289 = or i32 %288, 16
  %290 = add i32 %289, 188
  %291 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %292 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %290, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %293 = extractvalue %dx.types.ResRet.i32 %292, 0
  %294 = icmp eq i32 %293, 0
  %295 = add i32 %289, 192
  %296 = add i32 %289, 208
  br i1 %294, label %297, label %604

; <label>:297                                     ; preds = %284
  %298 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %295, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %299 = extractvalue %dx.types.ResRet.i32 %298, 0
  %300 = extractvalue %dx.types.ResRet.i32 %298, 1
  %301 = extractvalue %dx.types.ResRet.i32 %298, 2
  %302 = bitcast i32 %299 to float
  %303 = bitcast i32 %300 to float
  %304 = bitcast i32 %301 to float
  %305 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %296, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %306 = extractvalue %dx.types.ResRet.i32 %305, 0
  %307 = extractvalue %dx.types.ResRet.i32 %305, 1
  %308 = extractvalue %dx.types.ResRet.i32 %305, 2
  %309 = add i32 %289, 220
  %310 = bitcast i32 %306 to float
  %311 = bitcast i32 %307 to float
  %312 = bitcast i32 %308 to float
  %313 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %309, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %314 = extractvalue %dx.types.ResRet.i32 %313, 0
  %315 = add i32 %289, 224
  %316 = bitcast i32 %314 to float
  %317 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %315, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %318 = extractvalue %dx.types.ResRet.i32 %317, 0
  %319 = extractvalue %dx.types.ResRet.i32 %317, 1
  %320 = extractvalue %dx.types.ResRet.i32 %317, 2
  %321 = bitcast i32 %318 to float
  %322 = bitcast i32 %319 to float
  %323 = bitcast i32 %320 to float
  %324 = add i32 %289, 248
  %325 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %324, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %326 = extractvalue %dx.types.ResRet.i32 %325, 0
  %327 = icmp eq i32 %326, -1
  br i1 %327, label %332, label %328

; <label>:328                                     ; preds = %297
  %329 = lshr i32 %326, 16
  %330 = add nsw i32 %329, -1024
  %331 = and i32 %326, 65535
  br label %332

; <label>:332                                     ; preds = %328, %297
  %333 = phi i32 [ %330, %328 ], [ 1024, %297 ]
  %334 = phi i32 [ %331, %328 ], [ -1, %297 ]
  %335 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %336 = extractvalue %dx.types.CBufRet.f32 %335, 0
  %337 = extractvalue %dx.types.CBufRet.f32 %335, 1
  %338 = extractvalue %dx.types.CBufRet.f32 %335, 2
  %339 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %340 = extractvalue %dx.types.CBufRet.f32 %339, 0
  %341 = extractvalue %dx.types.CBufRet.f32 %339, 1
  %342 = extractvalue %dx.types.CBufRet.f32 %339, 2
  %343 = fsub float %302, %336
  %344 = fsub float %303, %337
  %345 = fsub float %304, %338
  %346 = fsub float %310, %340
  %347 = fsub float %311, %341
  %348 = fsub float %312, %342
  %349 = fadd float %343, %346
  %350 = fadd float %344, %347
  %351 = fadd float %345, %348
  %352 = fadd float %321, %349
  %353 = fadd float %322, %350
  %354 = fadd float %323, %351
  %355 = fadd float %114, %352
  %356 = fadd float %115, %353
  %357 = fadd float %116, %354
  %358 = fmul float %355, %355
  %359 = fmul float %356, %356
  %360 = fadd float %358, %359
  %361 = fmul float %357, %357
  %362 = fadd float %361, %360
  %363 = call float @dx.op.unary.f32(i32 24, float %362), !dx.precise !42  ; Sqrt(value)
  %364 = call float @dx.op.unary.f32(i32 23, float %363), !dx.precise !42  ; Log(value)
  %365 = fadd float %316, %364
  %366 = call float @dx.op.unary.f32(i32 27, float %365), !dx.precise !42  ; Round_ni(value)
  %367 = fptosi float %366 to i32
  %368 = sub nsw i32 %367, %333
  %369 = call i32 @dx.op.binary.i32(i32 37, i32 0, i32 %368)  ; IMax(a,b)
  %370 = icmp slt i32 %369, %334
  br i1 %370, label %371, label %843

; <label>:371                                     ; preds = %332
  %372 = add nsw i32 %369, %287
  %373 = mul i32 %372, 288
  %374 = or i32 %373, 16
  %375 = add i32 %374, 48
  %376 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %375, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %377 = extractvalue %dx.types.ResRet.i32 %376, 0
  %378 = extractvalue %dx.types.ResRet.i32 %376, 1
  %379 = extractvalue %dx.types.ResRet.i32 %376, 2
  %380 = add i32 %374, 64
  %381 = bitcast i32 %377 to float
  %382 = bitcast i32 %378 to float
  %383 = bitcast i32 %379 to float
  %384 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %380, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %385 = extractvalue %dx.types.ResRet.i32 %384, 0
  %386 = extractvalue %dx.types.ResRet.i32 %384, 1
  %387 = extractvalue %dx.types.ResRet.i32 %384, 2
  %388 = add i32 %374, 80
  %389 = bitcast i32 %385 to float
  %390 = bitcast i32 %386 to float
  %391 = bitcast i32 %387 to float
  %392 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %388, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %393 = extractvalue %dx.types.ResRet.i32 %392, 0
  %394 = extractvalue %dx.types.ResRet.i32 %392, 1
  %395 = extractvalue %dx.types.ResRet.i32 %392, 2
  %396 = add i32 %374, 96
  %397 = bitcast i32 %393 to float
  %398 = bitcast i32 %394 to float
  %399 = bitcast i32 %395 to float
  %400 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %396, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %401 = extractvalue %dx.types.ResRet.i32 %400, 0
  %402 = extractvalue %dx.types.ResRet.i32 %400, 1
  %403 = extractvalue %dx.types.ResRet.i32 %400, 2
  %404 = bitcast i32 %401 to float
  %405 = bitcast i32 %402 to float
  %406 = bitcast i32 %403 to float
  %407 = add i32 %374, 192
  %408 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %407, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %409 = extractvalue %dx.types.ResRet.i32 %408, 0
  %410 = extractvalue %dx.types.ResRet.i32 %408, 1
  %411 = extractvalue %dx.types.ResRet.i32 %408, 2
  %412 = bitcast i32 %409 to float
  %413 = bitcast i32 %410 to float
  %414 = bitcast i32 %411 to float
  %415 = add i32 %374, 208
  %416 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %415, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %417 = extractvalue %dx.types.ResRet.i32 %416, 0
  %418 = extractvalue %dx.types.ResRet.i32 %416, 1
  %419 = extractvalue %dx.types.ResRet.i32 %416, 2
  %420 = bitcast i32 %417 to float
  %421 = bitcast i32 %418 to float
  %422 = bitcast i32 %419 to float
  %423 = fsub float %412, %336
  %424 = fsub float %413, %337
  %425 = fsub float %414, %338
  %426 = fsub float %420, %340
  %427 = fsub float %421, %341
  %428 = fsub float %422, %342
  %429 = fadd float %423, %426
  %430 = fadd float %424, %427
  %431 = fadd float %425, %428
  %432 = fadd fast float %429, %114
  %433 = fadd fast float %430, %115
  %434 = fadd fast float %431, %116
  %435 = fmul fast float %432, %381
  %436 = call float @dx.op.tertiary.f32(i32 46, float %433, float %389, float %435)  ; FMad(a,b,c)
  %437 = call float @dx.op.tertiary.f32(i32 46, float %434, float %397, float %436)  ; FMad(a,b,c)
  %438 = fadd fast float %437, %404
  %439 = fmul fast float %432, %382
  %440 = call float @dx.op.tertiary.f32(i32 46, float %433, float %390, float %439)  ; FMad(a,b,c)
  %441 = call float @dx.op.tertiary.f32(i32 46, float %434, float %398, float %440)  ; FMad(a,b,c)
  %442 = fadd fast float %441, %405
  %443 = fmul fast float %432, %383
  %444 = call float @dx.op.tertiary.f32(i32 46, float %433, float %391, float %443)  ; FMad(a,b,c)
  %445 = call float @dx.op.tertiary.f32(i32 46, float %434, float %399, float %444)  ; FMad(a,b,c)
  %446 = fadd fast float %445, %406
  %447 = fmul fast float %438, 1.280000e+02
  %448 = fmul fast float %442, 1.280000e+02
  %449 = fptoui float %447 to i32
  %450 = fptoui float %448 to i32
  %451 = icmp slt i32 %372, 8192
  br i1 %451, label %452, label %455

; <label>:452                                     ; preds = %371
  %453 = lshr i32 %372, 7
  %454 = and i32 %372, 127
  br label %465

; <label>:455                                     ; preds = %371
  %456 = add i32 %372, -8191
  %457 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %20, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %458 = extractvalue %dx.types.CBufRet.i32 %457, 0
  %459 = and i32 %458, 31
  %460 = lshr i32 %456, %459
  %461 = mul i32 %460, 192
  %462 = extractvalue %dx.types.CBufRet.i32 %457, 1
  %463 = and i32 %462, %456
  %464 = shl i32 %463, 7
  br label %465

; <label>:465                                     ; preds = %455, %452
  %466 = phi i32 [ %454, %452 ], [ %464, %455 ]
  %467 = phi i32 [ %453, %452 ], [ %461, %455 ]
  %468 = select i1 %451, i32 0, i32 %449
  %469 = add i32 %466, %468
  %470 = select i1 %451, i32 0, i32 %450
  %471 = add i32 %467, %470
  %472 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 2, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2D<U32>
  %473 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %472, i32 0, i32 %469, i32 %471, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %474 = extractvalue %dx.types.ResRet.i32 %473, 0
  %475 = lshr i32 %474, 20
  %476 = and i32 %475, 63
  %477 = icmp slt i32 %474, 0
  br i1 %477, label %478, label %598

; <label>:478                                     ; preds = %465
  %479 = icmp eq i32 %476, 0
  %480 = zext i1 %479 to i32
  %481 = add i32 %476, %372
  %482 = fmul fast float %438, 1.638400e+04
  %483 = fmul fast float %442, 1.638400e+04
  %484 = fptoui float %482 to i32
  %485 = fptoui float %483 to i32
  br i1 %479, label %575, label %486

; <label>:486                                     ; preds = %478
  %487 = add i32 %374, 240
  %488 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %487, i32 undef, i8 3, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %489 = extractvalue %dx.types.ResRet.i32 %488, 0
  %490 = extractvalue %dx.types.ResRet.i32 %488, 1
  %491 = mul i32 %481, 288
  %492 = or i32 %491, 16
  %493 = add i32 %492, 240
  %494 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %493, i32 undef, i8 3, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %495 = extractvalue %dx.types.ResRet.i32 %494, 0
  %496 = extractvalue %dx.types.ResRet.i32 %494, 1
  %497 = shl i32 %489, 5
  %498 = shl i32 %490, 5
  %499 = shl i32 %495, 5
  %500 = shl i32 %496, 5
  %501 = sub i32 %449, %497
  %502 = sub i32 %450, %498
  %503 = and i32 %475, 31
  %504 = shl i32 %499, %503
  %505 = shl i32 %500, %503
  %506 = add i32 %501, %504
  %507 = add i32 %502, %505
  %508 = lshr i32 %506, %503
  %509 = lshr i32 %507, %503
  %510 = shl i32 %508, 7
  %511 = shl i32 %509, 7
  %512 = or i32 %510, 127
  %513 = or i32 %511, 127
  %514 = add i32 %374, 32
  %515 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %514, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %516 = extractvalue %dx.types.ResRet.i32 %515, 2
  %517 = bitcast i32 %516 to float
  %518 = add i32 %492, 32
  %519 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %518, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %520 = extractvalue %dx.types.ResRet.i32 %519, 2
  %521 = bitcast i32 %520 to float
  %522 = sitofp i32 %489 to float
  %523 = sitofp i32 %490 to float
  %524 = sitofp i32 %495 to float
  %525 = sitofp i32 %496 to float
  %526 = shl i32 1, %503
  %527 = uitofp i32 %526 to float
  %528 = fdiv fast float 1.000000e+00, %527
  %529 = fmul fast float %528, %522
  %530 = fmul fast float %528, %523
  %531 = fsub fast float %524, %529
  %532 = fsub fast float %525, %530
  %533 = fmul fast float %531, 2.500000e-01
  %534 = fmul fast float %532, 2.500000e-01
  %535 = fmul fast float %528, %517
  %536 = fsub fast float %521, %535
  %537 = fmul fast float %528, %438
  %538 = fmul fast float %528, %442
  %539 = fadd fast float %533, %537
  %540 = fadd fast float %534, %538
  %541 = fmul fast float %539, 1.638400e+04
  %542 = fmul fast float %540, 1.638400e+04
  %543 = fptoui float %541 to i32
  %544 = fptoui float %542 to i32
  %545 = call i32 @dx.op.binary.i32(i32 39, i32 %543, i32 %510)  ; UMax(a,b)
  %546 = call i32 @dx.op.binary.i32(i32 39, i32 %544, i32 %511)  ; UMax(a,b)
  %547 = call i32 @dx.op.binary.i32(i32 40, i32 %545, i32 %512)  ; UMin(a,b)
  %548 = call i32 @dx.op.binary.i32(i32 40, i32 %546, i32 %513)  ; UMin(a,b)
  %549 = icmp slt i32 %481, 8192
  br i1 %549, label %550, label %553

; <label>:550                                     ; preds = %486
  %551 = lshr i32 %481, 7
  %552 = and i32 %481, 127
  br label %563

; <label>:553                                     ; preds = %486
  %554 = add i32 %481, -8191
  %555 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %20, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %556 = extractvalue %dx.types.CBufRet.i32 %555, 0
  %557 = and i32 %556, 31
  %558 = lshr i32 %554, %557
  %559 = mul i32 %558, 192
  %560 = extractvalue %dx.types.CBufRet.i32 %555, 1
  %561 = and i32 %560, %554
  %562 = shl i32 %561, 7
  br label %563

; <label>:563                                     ; preds = %553, %550
  %564 = phi i32 [ %552, %550 ], [ %562, %553 ]
  %565 = phi i32 [ %551, %550 ], [ %559, %553 ]
  %566 = select i1 %549, i32 0, i32 %508
  %567 = add i32 %564, %566
  %568 = select i1 %549, i32 0, i32 %509
  %569 = add i32 %565, %568
  %570 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %472, i32 0, i32 %567, i32 %569, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %571 = extractvalue %dx.types.ResRet.i32 %570, 0
  %572 = and i32 %571, -2081423360
  %573 = icmp eq i32 %572, -2147483648
  %574 = zext i1 %573 to i32
  br label %575

; <label>:575                                     ; preds = %563, %478
  %576 = phi i32 [ %571, %563 ], [ %474, %478 ]
  %577 = phi i32 [ %574, %563 ], [ %480, %478 ]
  %578 = phi i32 [ %547, %563 ], [ %484, %478 ]
  %579 = phi i32 [ %548, %563 ], [ %485, %478 ]
  %580 = phi float [ %528, %563 ], [ 1.000000e+00, %478 ]
  %581 = phi float [ %536, %563 ], [ 0.000000e+00, %478 ]
  %582 = icmp eq i32 %577, 0
  br i1 %582, label %598, label %583

; <label>:583                                     ; preds = %575
  %584 = shl i32 %576, 7
  %585 = and i32 %584, 130944
  %586 = lshr i32 %576, 3
  %587 = and i32 %586, 130944
  %588 = and i32 %578, 127
  %589 = and i32 %579, 127
  %590 = or i32 %588, %585
  %591 = or i32 %589, %587
  %592 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 7, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2DArray<U32>
  %593 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %592, i32 0, i32 %590, i32 %591, i32 0, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %594 = extractvalue %dx.types.ResRet.i32 %593, 0
  %595 = bitcast i32 %594 to float
  %596 = fsub fast float %595, %581
  %597 = fdiv fast float %596, %580
  br label %598

; <label>:598                                     ; preds = %583, %575, %465
  %599 = phi float [ %597, %583 ], [ 0.000000e+00, %575 ], [ 0.000000e+00, %465 ]
  %600 = phi i1 [ true, %583 ], [ false, %575 ], [ false, %465 ]
  %601 = fcmp fast ogt float %599, %446
  %602 = and i1 %600, %601
  %603 = select i1 %602, float 0.000000e+00, float 1.000000e+00
  br label %843

; <label>:604                                     ; preds = %284
  %605 = add i32 %289, 48
  %606 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %605, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %607 = extractvalue %dx.types.ResRet.i32 %606, 0
  %608 = extractvalue %dx.types.ResRet.i32 %606, 1
  %609 = extractvalue %dx.types.ResRet.i32 %606, 2
  %610 = extractvalue %dx.types.ResRet.i32 %606, 3
  %611 = add i32 %289, 64
  %612 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %611, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %613 = extractvalue %dx.types.ResRet.i32 %612, 0
  %614 = extractvalue %dx.types.ResRet.i32 %612, 1
  %615 = extractvalue %dx.types.ResRet.i32 %612, 2
  %616 = extractvalue %dx.types.ResRet.i32 %612, 3
  %617 = add i32 %289, 80
  %618 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %617, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %619 = extractvalue %dx.types.ResRet.i32 %618, 0
  %620 = extractvalue %dx.types.ResRet.i32 %618, 1
  %621 = extractvalue %dx.types.ResRet.i32 %618, 2
  %622 = extractvalue %dx.types.ResRet.i32 %618, 3
  %623 = add i32 %289, 96
  %624 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %623, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %625 = extractvalue %dx.types.ResRet.i32 %624, 0
  %626 = extractvalue %dx.types.ResRet.i32 %624, 1
  %627 = extractvalue %dx.types.ResRet.i32 %624, 2
  %628 = extractvalue %dx.types.ResRet.i32 %624, 3
  %629 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %295, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %630 = extractvalue %dx.types.ResRet.i32 %629, 0
  %631 = extractvalue %dx.types.ResRet.i32 %629, 1
  %632 = extractvalue %dx.types.ResRet.i32 %629, 2
  %633 = bitcast i32 %630 to float
  %634 = bitcast i32 %631 to float
  %635 = bitcast i32 %632 to float
  %636 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %296, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %637 = extractvalue %dx.types.ResRet.i32 %636, 0
  %638 = extractvalue %dx.types.ResRet.i32 %636, 1
  %639 = extractvalue %dx.types.ResRet.i32 %636, 2
  %640 = bitcast i32 %637 to float
  %641 = bitcast i32 %638 to float
  %642 = bitcast i32 %639 to float
  %643 = add i32 %289, 268
  %644 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %643, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %645 = extractvalue %dx.types.ResRet.i32 %644, 0
  %646 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %647 = extractvalue %dx.types.CBufRet.f32 %646, 0
  %648 = extractvalue %dx.types.CBufRet.f32 %646, 1
  %649 = extractvalue %dx.types.CBufRet.f32 %646, 2
  %650 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %651 = extractvalue %dx.types.CBufRet.f32 %650, 0
  %652 = extractvalue %dx.types.CBufRet.f32 %650, 1
  %653 = extractvalue %dx.types.CBufRet.f32 %650, 2
  %654 = fsub float %633, %647
  %655 = fsub float %634, %648
  %656 = fsub float %635, %649
  %657 = fsub float %640, %651
  %658 = fsub float %641, %652
  %659 = fsub float %642, %653
  %660 = fadd float %654, %657
  %661 = fadd float %655, %658
  %662 = fadd float %656, %659
  %663 = fadd fast float %660, %114
  %664 = fadd fast float %661, %115
  %665 = fadd fast float %662, %116
  %666 = icmp eq i32 %293, 2
  br i1 %666, label %717, label %667

; <label>:667                                     ; preds = %604
  %668 = call float @dx.op.unary.f32(i32 6, float %663)  ; FAbs(value)
  %669 = call float @dx.op.unary.f32(i32 6, float %664)  ; FAbs(value)
  %670 = fcmp fast oge float %668, %669
  %671 = call float @dx.op.unary.f32(i32 6, float %665)  ; FAbs(value)
  %672 = fcmp fast oge float %668, %671
  %673 = and i1 %670, %672
  br i1 %673, label %674, label %677

; <label>:674                                     ; preds = %667
  %675 = fcmp ule float %663, 0.000000e+00
  %676 = zext i1 %675 to i32
  br label %685

; <label>:677                                     ; preds = %667
  %678 = fcmp fast ogt float %669, %671
  br i1 %678, label %679, label %682

; <label>:679                                     ; preds = %677
  %680 = fcmp fast ogt float %664, 0.000000e+00
  %681 = select i1 %680, i32 2, i32 3
  br label %685

; <label>:682                                     ; preds = %677
  %683 = fcmp fast ogt float %665, 0.000000e+00
  %684 = select i1 %683, i32 4, i32 5
  br label %685

; <label>:685                                     ; preds = %682, %679, %674
  %686 = phi i32 [ %676, %674 ], [ %681, %679 ], [ %684, %682 ]
  %687 = add i32 %686, %287
  %688 = mul i32 %687, 288
  %689 = or i32 %688, 16
  %690 = add i32 %689, 48
  %691 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %690, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %692 = extractvalue %dx.types.ResRet.i32 %691, 0
  %693 = extractvalue %dx.types.ResRet.i32 %691, 1
  %694 = extractvalue %dx.types.ResRet.i32 %691, 2
  %695 = extractvalue %dx.types.ResRet.i32 %691, 3
  %696 = add i32 %689, 64
  %697 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %696, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %698 = extractvalue %dx.types.ResRet.i32 %697, 0
  %699 = extractvalue %dx.types.ResRet.i32 %697, 1
  %700 = extractvalue %dx.types.ResRet.i32 %697, 2
  %701 = extractvalue %dx.types.ResRet.i32 %697, 3
  %702 = add i32 %689, 80
  %703 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %702, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %704 = extractvalue %dx.types.ResRet.i32 %703, 0
  %705 = extractvalue %dx.types.ResRet.i32 %703, 1
  %706 = extractvalue %dx.types.ResRet.i32 %703, 2
  %707 = extractvalue %dx.types.ResRet.i32 %703, 3
  %708 = add i32 %689, 96
  %709 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %708, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %710 = extractvalue %dx.types.ResRet.i32 %709, 0
  %711 = extractvalue %dx.types.ResRet.i32 %709, 1
  %712 = extractvalue %dx.types.ResRet.i32 %709, 2
  %713 = extractvalue %dx.types.ResRet.i32 %709, 3
  %714 = add i32 %689, 268
  %715 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %291, i32 %714, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %716 = extractvalue %dx.types.ResRet.i32 %715, 0
  br label %717

; <label>:717                                     ; preds = %685, %604
  %718 = phi i32 [ %692, %685 ], [ %607, %604 ]
  %719 = phi i32 [ %693, %685 ], [ %608, %604 ]
  %720 = phi i32 [ %694, %685 ], [ %609, %604 ]
  %721 = phi i32 [ %695, %685 ], [ %610, %604 ]
  %722 = phi i32 [ %698, %685 ], [ %613, %604 ]
  %723 = phi i32 [ %699, %685 ], [ %614, %604 ]
  %724 = phi i32 [ %700, %685 ], [ %615, %604 ]
  %725 = phi i32 [ %701, %685 ], [ %616, %604 ]
  %726 = phi i32 [ %704, %685 ], [ %619, %604 ]
  %727 = phi i32 [ %705, %685 ], [ %620, %604 ]
  %728 = phi i32 [ %706, %685 ], [ %621, %604 ]
  %729 = phi i32 [ %707, %685 ], [ %622, %604 ]
  %730 = phi i32 [ %710, %685 ], [ %625, %604 ]
  %731 = phi i32 [ %711, %685 ], [ %626, %604 ]
  %732 = phi i32 [ %712, %685 ], [ %627, %604 ]
  %733 = phi i32 [ %713, %685 ], [ %628, %604 ]
  %734 = phi i32 [ %716, %685 ], [ %645, %604 ]
  %735 = phi i32 [ %687, %685 ], [ %287, %604 ]
  %736 = bitcast i32 %733 to float
  %737 = bitcast i32 %732 to float
  %738 = bitcast i32 %731 to float
  %739 = bitcast i32 %730 to float
  %740 = bitcast i32 %729 to float
  %741 = bitcast i32 %728 to float
  %742 = bitcast i32 %727 to float
  %743 = bitcast i32 %726 to float
  %744 = bitcast i32 %725 to float
  %745 = bitcast i32 %724 to float
  %746 = bitcast i32 %723 to float
  %747 = bitcast i32 %722 to float
  %748 = bitcast i32 %721 to float
  %749 = bitcast i32 %720 to float
  %750 = bitcast i32 %719 to float
  %751 = bitcast i32 %718 to float
  %752 = fmul fast float %751, %663
  %753 = call float @dx.op.tertiary.f32(i32 46, float %664, float %747, float %752)  ; FMad(a,b,c)
  %754 = call float @dx.op.tertiary.f32(i32 46, float %665, float %743, float %753)  ; FMad(a,b,c)
  %755 = fadd fast float %754, %739
  %756 = fmul fast float %750, %663
  %757 = call float @dx.op.tertiary.f32(i32 46, float %664, float %746, float %756)  ; FMad(a,b,c)
  %758 = call float @dx.op.tertiary.f32(i32 46, float %665, float %742, float %757)  ; FMad(a,b,c)
  %759 = fadd fast float %758, %738
  %760 = fmul fast float %749, %663
  %761 = call float @dx.op.tertiary.f32(i32 46, float %664, float %745, float %760)  ; FMad(a,b,c)
  %762 = call float @dx.op.tertiary.f32(i32 46, float %665, float %741, float %761)  ; FMad(a,b,c)
  %763 = fadd fast float %762, %737
  %764 = fmul fast float %748, %663
  %765 = call float @dx.op.tertiary.f32(i32 46, float %664, float %744, float %764)  ; FMad(a,b,c)
  %766 = call float @dx.op.tertiary.f32(i32 46, float %665, float %740, float %765)  ; FMad(a,b,c)
  %767 = fadd fast float %766, %736
  %768 = fdiv fast float %755, %767
  %769 = fdiv fast float %759, %767
  %770 = fdiv fast float %763, %767
  %771 = fmul fast float %768, 1.280000e+02
  %772 = fmul fast float %769, 1.280000e+02
  %773 = fptoui float %771 to i32
  %774 = fptoui float %772 to i32
  %775 = and i32 %734, 31
  %776 = lshr i32 %773, %775
  %777 = lshr i32 %774, %775
  %778 = icmp slt i32 %735, 8192
  br i1 %778, label %779, label %782

; <label>:779                                     ; preds = %717
  %780 = lshr i32 %735, 7
  %781 = and i32 %735, 127
  br label %803

; <label>:782                                     ; preds = %717
  %783 = add i32 %735, -8191
  %784 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %20, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %785 = extractvalue %dx.types.CBufRet.i32 %784, 0
  %786 = and i32 %785, 31
  %787 = lshr i32 %783, %786
  %788 = mul i32 %787, 192
  %789 = extractvalue %dx.types.CBufRet.i32 %784, 1
  %790 = and i32 %789, %783
  %791 = shl i32 %790, 7
  %792 = icmp eq i32 %734, 0
  br i1 %792, label %798, label %793

; <label>:793                                     ; preds = %782
  %794 = sub i32 8, %734
  %795 = and i32 %794, 31
  %796 = shl i32 127, %795
  %797 = and i32 %796, 127
  br label %798

; <label>:798                                     ; preds = %793, %782
  %799 = phi i32 [ %797, %793 ], [ 0, %782 ]
  %800 = phi i32 [ 128, %793 ], [ 0, %782 ]
  %801 = or i32 %799, %791
  %802 = add i32 %800, %788
  br label %803

; <label>:803                                     ; preds = %798, %779
  %804 = phi i32 [ %781, %779 ], [ %801, %798 ]
  %805 = phi i32 [ %780, %779 ], [ %802, %798 ]
  %806 = select i1 %778, i32 0, i32 %776
  %807 = add i32 %804, %806
  %808 = select i1 %778, i32 0, i32 %777
  %809 = add i32 %805, %808
  %810 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 2, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2D<U32>
  %811 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %810, i32 0, i32 %807, i32 %809, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %812 = extractvalue %dx.types.ResRet.i32 %811, 0
  %813 = lshr i32 %812, 20
  %814 = icmp slt i32 %812, 0
  %815 = add i32 %813, %734
  %816 = and i32 %815, 31
  %817 = lshr i32 16384, %816
  %818 = uitofp i32 %817 to float
  %819 = select i1 %778, float 1.280000e+02, float %818
  br i1 %814, label %820, label %837

; <label>:820                                     ; preds = %803
  %821 = lshr i32 %812, 3
  %822 = and i32 %821, 130944
  %823 = fmul fast float %819, %769
  %824 = fptoui float %823 to i32
  %825 = and i32 %824, 127
  %826 = or i32 %825, %822
  %827 = shl i32 %812, 7
  %828 = and i32 %827, 130944
  %829 = fmul fast float %819, %768
  %830 = fptoui float %829 to i32
  %831 = and i32 %830, 127
  %832 = or i32 %831, %828
  %833 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 7, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2DArray<U32>
  %834 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %833, i32 0, i32 %832, i32 %826, i32 0, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %835 = extractvalue %dx.types.ResRet.i32 %834, 0
  %836 = bitcast i32 %835 to float
  br label %837

; <label>:837                                     ; preds = %820, %803
  %838 = phi float [ %836, %820 ], [ 0.000000e+00, %803 ]
  %839 = phi i1 [ true, %820 ], [ false, %803 ]
  %840 = fcmp fast ogt float %838, %770
  %841 = and i1 %839, %840
  %842 = select i1 %841, float 0.000000e+00, float 1.000000e+00
  br label %843

; <label>:843                                     ; preds = %837, %598, %332
  %844 = phi float [ 1.000000e+00, %332 ], [ %603, %598 ], [ %842, %837 ]
  %845 = fmul fast float %844, %285
  %846 = fsub fast float %114, %32
  %847 = fsub fast float %115, %33
  %848 = fsub fast float %116, %34
  %849 = fadd fast float %39, %38
  %850 = call float @dx.op.dot3.f32(i32 55, float %36, float %37, float %849, float %36, float %37, float %849)  ; Dot3(ax,ay,az,bx,by,bz)
  %851 = call float @dx.op.unary.f32(i32 25, float %850)  ; Rsqrt(value)
  %852 = fmul fast float %851, %36
  %853 = fmul fast float %851, %37
  %854 = fmul fast float %851, %849
  %855 = call float @dx.op.dot3.f32(i32 55, float %852, float %853, float %854, float %852, float %853, float %854)  ; Dot3(ax,ay,az,bx,by,bz)
  %856 = call float @dx.op.unary.f32(i32 25, float %855)  ; Rsqrt(value)
  %857 = fmul fast float %856, %852
  %858 = fmul fast float %856, %853
  %859 = fmul fast float %856, %854
  %860 = call float @dx.op.unary.f32(i32 6, float %859)  ; FAbs(value)
  %861 = fadd fast float %860, 0xBFC3333340000000
  %862 = fmul fast float %861, 2.000000e+00
  %863 = call float @dx.op.unary.f32(i32 7, float %862)  ; Saturate(value)
  %864 = call float @dx.op.dot3.f32(i32 55, float %857, float %858, float %859, float %41, float %42, float %43)  ; Dot3(ax,ay,az,bx,by,bz)
  %865 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %866 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %867 = extractvalue %dx.types.CBufRet.f32 %866, 0
  %868 = extractvalue %dx.types.CBufRet.f32 %866, 1
  %869 = extractvalue %dx.types.CBufRet.f32 %866, 2
  %870 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %871 = extractvalue %dx.types.CBufRet.f32 %870, 0
  %872 = extractvalue %dx.types.CBufRet.f32 %870, 1
  %873 = extractvalue %dx.types.CBufRet.f32 %870, 2
  %874 = fadd float %114, %867
  %875 = fadd float %115, %868
  %876 = fadd float %116, %869
  %877 = fsub float %874, %114
  %878 = fsub float %875, %115
  %879 = fsub float %876, %116
  %880 = fsub float %874, %877
  %881 = fsub float %875, %878
  %882 = fsub float %876, %879
  %883 = fsub float %114, %880
  %884 = fsub float %115, %881
  %885 = fsub float %116, %882
  %886 = fsub float %867, %877
  %887 = fsub float %868, %878
  %888 = fsub float %869, %879
  %889 = fadd float %886, %883
  %890 = fadd float %887, %884
  %891 = fadd float %888, %885
  %892 = fadd float %871, 0.000000e+00
  %893 = fadd float %872, 0.000000e+00
  %894 = fadd float %873, 0.000000e+00
  %895 = fsub float %892, %892
  %896 = fsub float %893, %893
  %897 = fsub float %894, %894
  %898 = fsub float 0.000000e+00, %895
  %899 = fsub float 0.000000e+00, %896
  %900 = fsub float 0.000000e+00, %897
  %901 = fsub float %871, %892
  %902 = fsub float %872, %893
  %903 = fsub float %873, %894
  %904 = fadd float %901, %898
  %905 = fadd float %902, %899
  %906 = fadd float %903, %900
  %907 = fadd float %892, %889
  %908 = fadd float %893, %890
  %909 = fadd float %894, %891
  %910 = fadd float %874, %907
  %911 = fadd float %875, %908
  %912 = fadd float %876, %909
  %913 = fsub float %910, %874
  %914 = fsub float %911, %875
  %915 = fsub float %912, %876
  %916 = fsub float %907, %913
  %917 = fsub float %908, %914
  %918 = fsub float %909, %915
  %919 = fadd float %904, %916
  %920 = fadd float %905, %917
  %921 = fadd float %906, %918
  %922 = fadd float %910, %919
  %923 = fadd float %911, %920
  %924 = fadd float %912, %921
  %925 = fsub float %922, %910
  %926 = fsub float %923, %911
  %927 = fsub float %924, %912
  %928 = fsub float %919, %925
  %929 = fsub float %920, %926
  %930 = fsub float %921, %927
  %931 = fadd float %922, %928
  %932 = fadd float %923, %929
  %933 = fadd float %924, %930
  %934 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 349)  ; CBufferLoadLegacy(handle,regIndex)
  %935 = extractvalue %dx.types.CBufRet.f32 %934, 0
  %936 = extractvalue %dx.types.CBufRet.f32 %934, 1
  %937 = extractvalue %dx.types.CBufRet.f32 %934, 3
  %938 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 350)  ; CBufferLoadLegacy(handle,regIndex)
  %939 = extractvalue %dx.types.CBufRet.f32 %938, 0
  %940 = extractvalue %dx.types.CBufRet.f32 %938, 1
  %941 = extractvalue %dx.types.CBufRet.f32 %938, 3
  %942 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 351)  ; CBufferLoadLegacy(handle,regIndex)
  %943 = extractvalue %dx.types.CBufRet.f32 %942, 0
  %944 = extractvalue %dx.types.CBufRet.f32 %942, 1
  %945 = extractvalue %dx.types.CBufRet.f32 %942, 3
  %946 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 352)  ; CBufferLoadLegacy(handle,regIndex)
  %947 = extractvalue %dx.types.CBufRet.f32 %946, 0
  %948 = extractvalue %dx.types.CBufRet.f32 %946, 1
  %949 = extractvalue %dx.types.CBufRet.f32 %946, 3
  %950 = fmul float %935, %931
  %951 = call float @dx.op.tertiary.f32(i32 46, float %932, float %939, float %950), !dx.precise !42  ; FMad(a,b,c)
  %952 = call float @dx.op.tertiary.f32(i32 46, float %933, float %943, float %951), !dx.precise !42  ; FMad(a,b,c)
  %953 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %947, float %952), !dx.precise !42  ; FMad(a,b,c)
  %954 = fmul float %936, %931
  %955 = call float @dx.op.tertiary.f32(i32 46, float %932, float %940, float %954), !dx.precise !42  ; FMad(a,b,c)
  %956 = call float @dx.op.tertiary.f32(i32 46, float %933, float %944, float %955), !dx.precise !42  ; FMad(a,b,c)
  %957 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %948, float %956), !dx.precise !42  ; FMad(a,b,c)
  %958 = fmul float %937, %931
  %959 = call float @dx.op.tertiary.f32(i32 46, float %932, float %941, float %958), !dx.precise !42  ; FMad(a,b,c)
  %960 = call float @dx.op.tertiary.f32(i32 46, float %933, float %945, float %959), !dx.precise !42  ; FMad(a,b,c)
  %961 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %949, float %960), !dx.precise !42  ; FMad(a,b,c)
  %962 = fdiv float %953, %961
  %963 = fdiv float %957, %961
  %964 = call float @dx.op.unary.f32(i32 6, float %962), !dx.precise !42  ; FAbs(value)
  %965 = call float @dx.op.unary.f32(i32 6, float %963), !dx.precise !42  ; FAbs(value)
  %966 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 365)  ; CBufferLoadLegacy(handle,regIndex)
  %967 = extractvalue %dx.types.CBufRet.f32 %966, 0
  %968 = extractvalue %dx.types.CBufRet.f32 %966, 1
  %969 = fcmp ole float %964, %967
  %970 = fcmp ole float %965, %968
  %971 = and i1 %969, %970
  %972 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 369)  ; CBufferLoadLegacy(handle,regIndex)
  %973 = extractvalue %dx.types.CBufRet.f32 %972, 0
  %974 = fcmp ogt float %973, 0xC7EFFFFFE0000000
  %975 = and i1 %971, %974
  br i1 %975, label %1106, label %976

; <label>:976                                     ; preds = %843
  %977 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 353)  ; CBufferLoadLegacy(handle,regIndex)
  %978 = extractvalue %dx.types.CBufRet.f32 %977, 0
  %979 = extractvalue %dx.types.CBufRet.f32 %977, 1
  %980 = extractvalue %dx.types.CBufRet.f32 %977, 3
  %981 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 354)  ; CBufferLoadLegacy(handle,regIndex)
  %982 = extractvalue %dx.types.CBufRet.f32 %981, 0
  %983 = extractvalue %dx.types.CBufRet.f32 %981, 1
  %984 = extractvalue %dx.types.CBufRet.f32 %981, 3
  %985 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 355)  ; CBufferLoadLegacy(handle,regIndex)
  %986 = extractvalue %dx.types.CBufRet.f32 %985, 0
  %987 = extractvalue %dx.types.CBufRet.f32 %985, 1
  %988 = extractvalue %dx.types.CBufRet.f32 %985, 3
  %989 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 356)  ; CBufferLoadLegacy(handle,regIndex)
  %990 = extractvalue %dx.types.CBufRet.f32 %989, 0
  %991 = extractvalue %dx.types.CBufRet.f32 %989, 1
  %992 = extractvalue %dx.types.CBufRet.f32 %989, 3
  %993 = fmul float %931, %978
  %994 = call float @dx.op.tertiary.f32(i32 46, float %932, float %982, float %993), !dx.precise !42  ; FMad(a,b,c)
  %995 = call float @dx.op.tertiary.f32(i32 46, float %933, float %986, float %994), !dx.precise !42  ; FMad(a,b,c)
  %996 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %990, float %995), !dx.precise !42  ; FMad(a,b,c)
  %997 = fmul float %931, %979
  %998 = call float @dx.op.tertiary.f32(i32 46, float %932, float %983, float %997), !dx.precise !42  ; FMad(a,b,c)
  %999 = call float @dx.op.tertiary.f32(i32 46, float %933, float %987, float %998), !dx.precise !42  ; FMad(a,b,c)
  %1000 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %991, float %999), !dx.precise !42  ; FMad(a,b,c)
  %1001 = fmul float %931, %980
  %1002 = call float @dx.op.tertiary.f32(i32 46, float %932, float %984, float %1001), !dx.precise !42  ; FMad(a,b,c)
  %1003 = call float @dx.op.tertiary.f32(i32 46, float %933, float %988, float %1002), !dx.precise !42  ; FMad(a,b,c)
  %1004 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %992, float %1003), !dx.precise !42  ; FMad(a,b,c)
  %1005 = fdiv float %996, %1004
  %1006 = fdiv float %1000, %1004
  %1007 = call float @dx.op.unary.f32(i32 6, float %1005), !dx.precise !42  ; FAbs(value)
  %1008 = call float @dx.op.unary.f32(i32 6, float %1006), !dx.precise !42  ; FAbs(value)
  %1009 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 366)  ; CBufferLoadLegacy(handle,regIndex)
  %1010 = extractvalue %dx.types.CBufRet.f32 %1009, 0
  %1011 = extractvalue %dx.types.CBufRet.f32 %1009, 1
  %1012 = fcmp ole float %1007, %1010
  %1013 = fcmp ole float %1008, %1011
  %1014 = and i1 %1012, %1013
  %1015 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 370)  ; CBufferLoadLegacy(handle,regIndex)
  %1016 = extractvalue %dx.types.CBufRet.f32 %1015, 0
  %1017 = fcmp ogt float %1016, 0xC7EFFFFFE0000000
  %1018 = and i1 %1014, %1017
  br i1 %1018, label %1106, label %1019

; <label>:1019                                    ; preds = %976
  %1020 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 357)  ; CBufferLoadLegacy(handle,regIndex)
  %1021 = extractvalue %dx.types.CBufRet.f32 %1020, 0
  %1022 = extractvalue %dx.types.CBufRet.f32 %1020, 1
  %1023 = extractvalue %dx.types.CBufRet.f32 %1020, 3
  %1024 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 358)  ; CBufferLoadLegacy(handle,regIndex)
  %1025 = extractvalue %dx.types.CBufRet.f32 %1024, 0
  %1026 = extractvalue %dx.types.CBufRet.f32 %1024, 1
  %1027 = extractvalue %dx.types.CBufRet.f32 %1024, 3
  %1028 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 359)  ; CBufferLoadLegacy(handle,regIndex)
  %1029 = extractvalue %dx.types.CBufRet.f32 %1028, 0
  %1030 = extractvalue %dx.types.CBufRet.f32 %1028, 1
  %1031 = extractvalue %dx.types.CBufRet.f32 %1028, 3
  %1032 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 360)  ; CBufferLoadLegacy(handle,regIndex)
  %1033 = extractvalue %dx.types.CBufRet.f32 %1032, 0
  %1034 = extractvalue %dx.types.CBufRet.f32 %1032, 1
  %1035 = extractvalue %dx.types.CBufRet.f32 %1032, 3
  %1036 = fmul float %931, %1021
  %1037 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1025, float %1036), !dx.precise !42  ; FMad(a,b,c)
  %1038 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1029, float %1037), !dx.precise !42  ; FMad(a,b,c)
  %1039 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1033, float %1038), !dx.precise !42  ; FMad(a,b,c)
  %1040 = fmul float %931, %1022
  %1041 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1026, float %1040), !dx.precise !42  ; FMad(a,b,c)
  %1042 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1030, float %1041), !dx.precise !42  ; FMad(a,b,c)
  %1043 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1034, float %1042), !dx.precise !42  ; FMad(a,b,c)
  %1044 = fmul float %931, %1023
  %1045 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1027, float %1044), !dx.precise !42  ; FMad(a,b,c)
  %1046 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1031, float %1045), !dx.precise !42  ; FMad(a,b,c)
  %1047 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1035, float %1046), !dx.precise !42  ; FMad(a,b,c)
  %1048 = fdiv float %1039, %1047
  %1049 = fdiv float %1043, %1047
  %1050 = call float @dx.op.unary.f32(i32 6, float %1048), !dx.precise !42  ; FAbs(value)
  %1051 = call float @dx.op.unary.f32(i32 6, float %1049), !dx.precise !42  ; FAbs(value)
  %1052 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 367)  ; CBufferLoadLegacy(handle,regIndex)
  %1053 = extractvalue %dx.types.CBufRet.f32 %1052, 0
  %1054 = extractvalue %dx.types.CBufRet.f32 %1052, 1
  %1055 = fcmp ole float %1050, %1053
  %1056 = fcmp ole float %1051, %1054
  %1057 = and i1 %1055, %1056
  %1058 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 371)  ; CBufferLoadLegacy(handle,regIndex)
  %1059 = extractvalue %dx.types.CBufRet.f32 %1058, 0
  %1060 = fcmp ogt float %1059, 0xC7EFFFFFE0000000
  %1061 = and i1 %1057, %1060
  br i1 %1061, label %1106, label %1062

; <label>:1062                                    ; preds = %1019
  %1063 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 361)  ; CBufferLoadLegacy(handle,regIndex)
  %1064 = extractvalue %dx.types.CBufRet.f32 %1063, 0
  %1065 = extractvalue %dx.types.CBufRet.f32 %1063, 1
  %1066 = extractvalue %dx.types.CBufRet.f32 %1063, 3
  %1067 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 362)  ; CBufferLoadLegacy(handle,regIndex)
  %1068 = extractvalue %dx.types.CBufRet.f32 %1067, 0
  %1069 = extractvalue %dx.types.CBufRet.f32 %1067, 1
  %1070 = extractvalue %dx.types.CBufRet.f32 %1067, 3
  %1071 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 363)  ; CBufferLoadLegacy(handle,regIndex)
  %1072 = extractvalue %dx.types.CBufRet.f32 %1071, 0
  %1073 = extractvalue %dx.types.CBufRet.f32 %1071, 1
  %1074 = extractvalue %dx.types.CBufRet.f32 %1071, 3
  %1075 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 364)  ; CBufferLoadLegacy(handle,regIndex)
  %1076 = extractvalue %dx.types.CBufRet.f32 %1075, 0
  %1077 = extractvalue %dx.types.CBufRet.f32 %1075, 1
  %1078 = extractvalue %dx.types.CBufRet.f32 %1075, 3
  %1079 = fmul float %931, %1064
  %1080 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1068, float %1079), !dx.precise !42  ; FMad(a,b,c)
  %1081 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1072, float %1080), !dx.precise !42  ; FMad(a,b,c)
  %1082 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1076, float %1081), !dx.precise !42  ; FMad(a,b,c)
  %1083 = fmul float %931, %1065
  %1084 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1069, float %1083), !dx.precise !42  ; FMad(a,b,c)
  %1085 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1073, float %1084), !dx.precise !42  ; FMad(a,b,c)
  %1086 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1077, float %1085), !dx.precise !42  ; FMad(a,b,c)
  %1087 = fmul float %931, %1066
  %1088 = call float @dx.op.tertiary.f32(i32 46, float %932, float %1070, float %1087), !dx.precise !42  ; FMad(a,b,c)
  %1089 = call float @dx.op.tertiary.f32(i32 46, float %933, float %1074, float %1088), !dx.precise !42  ; FMad(a,b,c)
  %1090 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %1078, float %1089), !dx.precise !42  ; FMad(a,b,c)
  %1091 = fdiv float %1082, %1090
  %1092 = fdiv float %1086, %1090
  %1093 = call float @dx.op.unary.f32(i32 6, float %1091), !dx.precise !42  ; FAbs(value)
  %1094 = call float @dx.op.unary.f32(i32 6, float %1092), !dx.precise !42  ; FAbs(value)
  %1095 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 368)  ; CBufferLoadLegacy(handle,regIndex)
  %1096 = extractvalue %dx.types.CBufRet.f32 %1095, 0
  %1097 = extractvalue %dx.types.CBufRet.f32 %1095, 1
  %1098 = fcmp ole float %1093, %1096
  %1099 = fcmp ole float %1094, %1097
  %1100 = and i1 %1098, %1099
  %1101 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 372)  ; CBufferLoadLegacy(handle,regIndex)
  %1102 = extractvalue %dx.types.CBufRet.f32 %1101, 0
  %1103 = fcmp ogt float %1102, 0xC7EFFFFFE0000000
  %1104 = and i1 %1100, %1103
  %1105 = select i1 %1104, i32 3, i32 -1
  br label %1106

; <label>:1106                                    ; preds = %1062, %1019, %976, %843
  %1107 = phi i32 [ 0, %843 ], [ 1, %976 ], [ 2, %1019 ], [ %1105, %1062 ]
  %1108 = icmp eq i32 %1107, -1
  %1109 = add nsw i32 %1107, 369
  %1110 = select i1 %1108, i32 369, i32 %1109
  %1111 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 %1110)  ; CBufferLoadLegacy(handle,regIndex)
  %1112 = extractvalue %dx.types.CBufRet.f32 %1111, 0
  %1113 = fsub fast float %1112, %848
  %1114 = extractvalue %dx.types.CBufRet.f32 %865, 1
  %1115 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 163)  ; CBufferLoadLegacy(handle,regIndex)
  %1116 = extractvalue %dx.types.CBufRet.f32 %1115, 2
  %1117 = extractvalue %dx.types.CBufRet.f32 %865, 2
  %1118 = extractvalue %dx.types.CBufRet.f32 %865, 3
  %1119 = fmul fast float %1117, %1116
  %1120 = fmul fast float %1118, %1116
  %1121 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %1122 = extractvalue %dx.types.CBufRet.f32 %1121, 0
  %1123 = fmul fast float %1122, %846
  %1124 = fmul fast float %1122, %847
  %1125 = extractvalue %dx.types.CBufRet.f32 %1121, 1
  %1126 = fmul fast float %1125, %846
  %1127 = fmul fast float %1125, %847
  %1128 = extractvalue %dx.types.CBufRet.f32 %1121, 2
  %1129 = fmul fast float %1128, %1116
  %1130 = fadd fast float %1129, %1127
  %1131 = fmul fast float %28, 2.097152e+06
  %1132 = fmul fast float %1131, %1125
  %1133 = call float @dx.op.unary.f32(i32 22, float %1132)  ; Frc(value)
  %1134 = fadd fast float %1133, %1126
  %1135 = fadd fast float %1134, %1129
  %1136 = fmul fast float %29, 2.097152e+06
  %1137 = fmul fast float %1136, %1125
  %1138 = call float @dx.op.unary.f32(i32 22, float %1137)  ; Frc(value)
  %1139 = fadd fast float %1130, %1138
  %1140 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %1141 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %12, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %1142 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %1140, %dx.types.Handle %1141, float %1135, float %1139, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %1143 = extractvalue %dx.types.ResRet.f32 %1142, 0
  %1144 = extractvalue %dx.types.ResRet.f32 %1142, 1
  %1145 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %1146 = extractvalue %dx.types.CBufRet.f32 %1145, 3
  %1147 = fmul fast float %1146, %1143
  %1148 = fmul fast float %1146, %1144
  %1149 = fmul fast float %1131, %1122
  %1150 = call float @dx.op.unary.f32(i32 22, float %1149)  ; Frc(value)
  %1151 = fadd fast float %1119, %41
  %1152 = fadd fast float %1151, %1123
  %1153 = fadd fast float %1152, %1150
  %1154 = fadd fast float %1153, %1147
  %1155 = fmul fast float %1136, %1122
  %1156 = call float @dx.op.unary.f32(i32 22, float %1155)  ; Frc(value)
  %1157 = fadd fast float %1120, %42
  %1158 = fadd fast float %1157, %1124
  %1159 = fadd fast float %1158, %1148
  %1160 = fadd fast float %1159, %1156
  %1161 = fmul fast float %30, -2.097152e+06
  %1162 = fadd fast float %1161, %1113
  %1163 = fmul fast float %1162, %1114
  %1164 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %1165 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %11, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %1166 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %1164, %dx.types.Handle %1165, float %1154, float %1160, float undef, float undef, i32 0, i32 0, i32 undef, float %1163)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %1167 = extractvalue %dx.types.ResRet.f32 %1166, 0
  %1168 = extractvalue %dx.types.ResRet.f32 %1166, 1
  %1169 = extractvalue %dx.types.ResRet.f32 %1166, 2
  %1170 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %1171 = extractvalue %dx.types.CBufRet.f32 %1170, 2
  %1172 = fcmp fast ole float %1167, 0x3E60000040000000
  %1173 = call float @dx.op.unary.f32(i32 23, float %1167)  ; Log(value)
  %1174 = fmul fast float %1173, %1171
  %1175 = call float @dx.op.unary.f32(i32 21, float %1174)  ; Exp(value)
  %1176 = select i1 %1172, float 0.000000e+00, float %1175
  %1177 = fcmp fast ole float %1168, 0x3E60000040000000
  %1178 = call float @dx.op.unary.f32(i32 23, float %1168)  ; Log(value)
  %1179 = fmul fast float %1178, %1171
  %1180 = call float @dx.op.unary.f32(i32 21, float %1179)  ; Exp(value)
  %1181 = select i1 %1177, float 0.000000e+00, float %1180
  %1182 = fcmp fast ole float %1169, 0x3E60000040000000
  %1183 = call float @dx.op.unary.f32(i32 23, float %1169)  ; Log(value)
  %1184 = fmul fast float %1183, %1171
  %1185 = call float @dx.op.unary.f32(i32 21, float %1184)  ; Exp(value)
  %1186 = select i1 %1182, float 0.000000e+00, float %1185
  %1187 = call float @dx.op.unary.f32(i32 7, float %1176)  ; Saturate(value)
  %1188 = call float @dx.op.unary.f32(i32 7, float %1181)  ; Saturate(value)
  %1189 = call float @dx.op.unary.f32(i32 7, float %1186)  ; Saturate(value)
  %1190 = extractvalue %dx.types.CBufRet.f32 %1170, 3
  %1191 = fmul fast float %1190, %1187
  %1192 = fmul fast float %1190, %1188
  %1193 = fmul fast float %1190, %1189
  %1194 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %1195 = extractvalue %dx.types.CBufRet.f32 %1194, 1
  %1196 = fcmp fast oge float %864, %1195
  %1197 = select i1 %1196, float %1191, float 0.000000e+00
  %1198 = select i1 %1196, float %1192, float 0.000000e+00
  %1199 = select i1 %1196, float %1193, float 0.000000e+00
  %1200 = fmul fast float %1197, %863
  %1201 = fmul fast float %1198, %863
  %1202 = fmul fast float %1199, %863
  %1203 = extractvalue %dx.types.CBufRet.f32 %1194, 3
  %1204 = extractvalue %dx.types.CBufRet.f32 %1194, 2
  %1205 = fsub fast float %1113, %1203
  %1206 = fsub fast float %1204, %1203
  %1207 = fdiv fast float 1.000000e+00, %1206
  %1208 = fadd fast float %1161, %1205
  %1209 = fmul fast float %1208, %1207
  %1210 = call float @dx.op.unary.f32(i32 7, float %1209)  ; Saturate(value)
  %1211 = fmul fast float %1210, %1210
  %1212 = fmul fast float %1210, 2.000000e+00
  %1213 = fsub fast float 3.000000e+00, %1212
  %1214 = fmul fast float %1211, %1213
  %1215 = fsub fast float 1.000000e+00, %1214
  %1216 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %1217 = extractvalue %dx.types.CBufRet.f32 %1216, 0
  %1218 = fcmp fast ole float %1215, 0x3E60000040000000
  %1219 = call float @dx.op.unary.f32(i32 23, float %1215)  ; Log(value)
  %1220 = fmul fast float %1219, %1217
  %1221 = call float @dx.op.unary.f32(i32 21, float %1220)  ; Exp(value)
  %1222 = select i1 %1218, float 0.000000e+00, float %1221
  %1223 = extractvalue %dx.types.CBufRet.f32 %1216, 1
  %1224 = fdiv fast float 1.000000e+00, %1223
  %1225 = fmul fast float %1162, %1224
  %1226 = call float @dx.op.unary.f32(i32 7, float %1225)  ; Saturate(value)
  %1227 = fmul fast float %1226, %1226
  %1228 = fmul fast float %1226, 2.000000e+00
  %1229 = fsub fast float 3.000000e+00, %1228
  %1230 = extractvalue %dx.types.CBufRet.f32 %1216, 2
  %1231 = fmul fast float %1227, %1222
  %1232 = fmul fast float %1231, %1229
  %1233 = fmul fast float %1232, %1230
  %1234 = fadd fast float %1200, -1.000000e+00
  %1235 = fadd fast float %1201, -1.000000e+00
  %1236 = fadd fast float %1202, -1.000000e+00
  %1237 = fmul fast float %1233, %1234
  %1238 = fmul fast float %1233, %1235
  %1239 = fmul fast float %1233, %1236
  %1240 = fadd fast float %1237, 1.000000e+00
  %1241 = fadd fast float %1238, 1.000000e+00
  %1242 = fadd fast float %1239, 1.000000e+00
  %1243 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 347)  ; CBufferLoadLegacy(handle,regIndex)
  %1244 = extractvalue %dx.types.CBufRet.f32 %1243, 1
  %1245 = fcmp fast ogt float %1244, 0.000000e+00
  %1246 = extractvalue %dx.types.CBufRet.f32 %1243, 0
  %1247 = fcmp fast une float %1246, 0.000000e+00
  %1248 = and i1 %1245, %1247
  %1249 = select i1 %1248, float %1240, float 1.000000e+00
  %1250 = select i1 %1248, float %1241, float 1.000000e+00
  %1251 = select i1 %1248, float %1242, float 1.000000e+00
  %1252 = extractvalue %dx.types.CBufRet.f32 %1216, 3
  %1253 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %19, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %1254 = extractvalue %dx.types.CBufRet.f32 %1253, 0
  %1255 = extractvalue %dx.types.CBufRet.f32 %1253, 1
  %1256 = extractvalue %dx.types.CBufRet.f32 %1253, 2
  %1257 = fsub fast float %1254, %1249
  %1258 = fsub fast float %1255, %1250
  %1259 = fsub fast float %1256, %1251
  %1260 = fmul fast float %1257, %1252
  %1261 = fmul fast float %1258, %1252
  %1262 = fmul fast float %1259, %1252
  %1263 = fadd fast float %1260, %1249
  %1264 = fadd fast float %1261, %1250
  %1265 = fadd fast float %1262, %1251
  %1266 = call float @dx.op.binary.f32(i32 35, float %1263, float 0.000000e+00)  ; FMax(a,b)
  %1267 = call float @dx.op.binary.f32(i32 35, float %1264, float 0.000000e+00)  ; FMax(a,b)
  %1268 = call float @dx.op.binary.f32(i32 35, float %1265, float 0.000000e+00)  ; FMax(a,b)
  %1269 = call float @dx.op.dot3.f32(i32 55, float %1266, float %1267, float %1268, float 0x3FD554C980000000, float 0x3FD554C980000000, float 0x3FD554C980000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1270 = fmul fast float %845, %1269
  %1271 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 13)  ; CBufferLoadLegacy(handle,regIndex)
  %1272 = extractvalue %dx.types.CBufRet.i32 %1271, 1
  %1273 = icmp eq i32 %1272, 0
  br i1 %1273, label %1343, label %1274

; <label>:1274                                    ; preds = %1106
  %1275 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 13)  ; CBufferLoadLegacy(handle,regIndex)
  %1276 = extractvalue %dx.types.CBufRet.f32 %1275, 0
  %1277 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %1278 = extractvalue %dx.types.CBufRet.f32 %1277, 1
  %1279 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 9)  ; CBufferLoadLegacy(handle,regIndex)
  %1280 = extractvalue %dx.types.CBufRet.f32 %1279, 0
  %1281 = extractvalue %dx.types.CBufRet.f32 %1279, 1
  %1282 = extractvalue %dx.types.CBufRet.f32 %1279, 2
  %1283 = extractvalue %dx.types.CBufRet.f32 %1279, 3
  %1284 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 10)  ; CBufferLoadLegacy(handle,regIndex)
  %1285 = extractvalue %dx.types.CBufRet.f32 %1284, 0
  %1286 = extractvalue %dx.types.CBufRet.f32 %1284, 1
  %1287 = extractvalue %dx.types.CBufRet.f32 %1284, 2
  %1288 = extractvalue %dx.types.CBufRet.f32 %1284, 3
  %1289 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 11)  ; CBufferLoadLegacy(handle,regIndex)
  %1290 = extractvalue %dx.types.CBufRet.f32 %1289, 0
  %1291 = extractvalue %dx.types.CBufRet.f32 %1289, 1
  %1292 = extractvalue %dx.types.CBufRet.f32 %1289, 2
  %1293 = extractvalue %dx.types.CBufRet.f32 %1289, 3
  %1294 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %23, i32 12)  ; CBufferLoadLegacy(handle,regIndex)
  %1295 = extractvalue %dx.types.CBufRet.f32 %1294, 0
  %1296 = extractvalue %dx.types.CBufRet.f32 %1294, 1
  %1297 = extractvalue %dx.types.CBufRet.f32 %1294, 2
  %1298 = extractvalue %dx.types.CBufRet.f32 %1294, 3
  %1299 = fmul fast float %1280, %114
  %1300 = call float @dx.op.tertiary.f32(i32 46, float %115, float %1285, float %1299)  ; FMad(a,b,c)
  %1301 = call float @dx.op.tertiary.f32(i32 46, float %116, float %1290, float %1300)  ; FMad(a,b,c)
  %1302 = fadd fast float %1301, %1295
  %1303 = fmul fast float %1281, %114
  %1304 = call float @dx.op.tertiary.f32(i32 46, float %115, float %1286, float %1303)  ; FMad(a,b,c)
  %1305 = call float @dx.op.tertiary.f32(i32 46, float %116, float %1291, float %1304)  ; FMad(a,b,c)
  %1306 = fadd fast float %1305, %1296
  %1307 = fmul fast float %1282, %114
  %1308 = call float @dx.op.tertiary.f32(i32 46, float %115, float %1287, float %1307)  ; FMad(a,b,c)
  %1309 = call float @dx.op.tertiary.f32(i32 46, float %116, float %1292, float %1308)  ; FMad(a,b,c)
  %1310 = fadd fast float %1309, %1297
  %1311 = fmul fast float %1283, %114
  %1312 = call float @dx.op.tertiary.f32(i32 46, float %115, float %1288, float %1311)  ; FMad(a,b,c)
  %1313 = call float @dx.op.tertiary.f32(i32 46, float %116, float %1293, float %1312)  ; FMad(a,b,c)
  %1314 = fadd fast float %1313, %1298
  %1315 = fdiv fast float %1302, %1314
  %1316 = fdiv fast float %1306, %1314
  %1317 = fdiv fast float %1310, %1314
  %1318 = fmul fast float %1315, 5.000000e-01
  %1319 = fmul fast float %1316, 5.000000e-01
  %1320 = fadd fast float %1318, 5.000000e-01
  %1321 = fsub fast float 5.000000e-01, %1319
  %1322 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 777 })  ; AnnotateHandle(res,props)  resource: Texture2D<3xF32>
  %1323 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %9, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %1324 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %1322, %dx.types.Handle %1323, float %1320, float %1321, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %1325 = extractvalue %dx.types.ResRet.f32 %1324, 0
  %1326 = extractvalue %dx.types.ResRet.f32 %1324, 1
  %1327 = extractvalue %dx.types.ResRet.f32 %1324, 2
  %1328 = fsub fast float 1.000000e+00, %1317
  %1329 = call float @dx.op.unary.f32(i32 7, float %1328)  ; Saturate(value)
  %1330 = fmul fast float %1329, %1278
  %1331 = fsub fast float %1330, %1325
  %1332 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %1331)  ; FMax(a,b)
  %1333 = fmul fast float %1326, 1.000000e+03
  %1334 = fmul fast float %1333, %1332
  %1335 = call float @dx.op.binary.f32(i32 36, float %1327, float %1334)  ; FMin(a,b)
  %1336 = fmul fast float %1335, 0xBFF7154760000000
  %1337 = call float @dx.op.unary.f32(i32 21, float %1336)  ; Exp(value)
  %1338 = call float @dx.op.unary.f32(i32 7, float %1337)  ; Saturate(value)
  %1339 = fadd fast float %1338, -1.000000e+00
  %1340 = fmul fast float %1339, %1276
  %1341 = fadd fast float %1340, 1.000000e+00
  %1342 = fmul fast float %1341, %1270
  br label %1343

; <label>:1343                                    ; preds = %1274, %1106
  %1344 = phi float [ %1342, %1274 ], [ %1270, %1106 ]
  %1345 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %23, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %1346 = extractvalue %dx.types.CBufRet.i32 %1345, 0
  %1347 = icmp eq i32 %1346, 1
  br i1 %1347, label %1348, label %1379

; <label>:1348                                    ; preds = %1343
  %1349 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 171)  ; CBufferLoadLegacy(handle,regIndex)
  %1350 = extractvalue %dx.types.CBufRet.f32 %1349, 3
  %1351 = fmul fast float %1350, %50
  %1352 = fmul fast float %1350, 4.000000e+00
  %1353 = fadd fast float %1352, 1.000000e+00
  %1354 = fmul fast float %1353, %25
  %1355 = fmul fast float %1353, %26
  %1356 = fmul fast float %1351, %1353
  %1357 = fmul fast float %1350, 2.000000e+00
  %1358 = fsub fast float -5.000000e-01, %1357
  %1359 = fadd fast float %1358, %1354
  %1360 = fadd fast float %1358, %1355
  %1361 = fadd fast float %1358, %1356
  %1362 = call float @dx.op.unary.f32(i32 6, float %1359)  ; FAbs(value)
  %1363 = call float @dx.op.unary.f32(i32 6, float %1360)  ; FAbs(value)
  %1364 = call float @dx.op.unary.f32(i32 6, float %1361)  ; FAbs(value)
  %1365 = fsub fast float 5.000000e-01, %1362
  %1366 = fsub fast float 5.000000e-01, %1363
  %1367 = fsub fast float 5.000000e-01, %1364
  %1368 = fmul fast float %1365, 1.000000e+01
  %1369 = fmul fast float %1366, 1.000000e+01
  %1370 = fmul fast float %1367, 1.000000e+01
  %1371 = call float @dx.op.unary.f32(i32 7, float %1368)  ; Saturate(value)
  %1372 = call float @dx.op.unary.f32(i32 7, float %1369)  ; Saturate(value)
  %1373 = call float @dx.op.unary.f32(i32 7, float %1370)  ; Saturate(value)
  %1374 = fadd fast float %1344, -1.000000e+00
  %1375 = fmul fast float %1371, %1374
  %1376 = fmul fast float %1375, %1372
  %1377 = fmul fast float %1376, %1373
  %1378 = fadd fast float %1377, 1.000000e+00
  br label %1379

; <label>:1379                                    ; preds = %1348, %1343
  %1380 = phi float [ %1378, %1348 ], [ %1344, %1343 ]
  %1381 = fmul fast float %67, 0x3FD45F3060000000
  %1382 = fmul fast float %68, 0x3FD45F3060000000
  %1383 = fmul fast float %69, 0x3FD45F3060000000
  %1384 = fmul fast float %1381, %1380
  %1385 = fmul fast float %1382, %1380
  %1386 = fmul fast float %1383, %1380
  %1387 = extractvalue %dx.types.CBufRet.i32 %1271, 2
  %1388 = icmp eq i32 %1387, 0
  br i1 %1388, label %1474, label %1389

; <label>:1389                                    ; preds = %1379
  %1390 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 194)  ; CBufferLoadLegacy(handle,regIndex)
  %1391 = extractvalue %dx.types.CBufRet.f32 %1390, 0
  %1392 = extractvalue %dx.types.CBufRet.f32 %1390, 1
  %1393 = extractvalue %dx.types.CBufRet.f32 %1390, 2
  %1394 = fsub fast float %114, %1391
  %1395 = fsub fast float %115, %1392
  %1396 = fsub fast float %116, %1393
  %1397 = fmul fast float %1394, 0x3EE4F8B580000000
  %1398 = fmul fast float %1395, 0x3EE4F8B580000000
  %1399 = fmul fast float %1396, 0x3EE4F8B580000000
  %1400 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 200)  ; CBufferLoadLegacy(handle,regIndex)
  %1401 = extractvalue %dx.types.CBufRet.f32 %1400, 3
  %1402 = extractvalue %dx.types.CBufRet.f32 %1400, 2
  %1403 = call float @dx.op.dot3.f32(i32 55, float %1397, float %1398, float %1399, float %1397, float %1398, float %1399)  ; Dot3(ax,ay,az,bx,by,bz)
  %1404 = call float @dx.op.dot3.f32(i32 55, float %100, float %101, float %102, float %100, float %101, float %102)  ; Dot3(ax,ay,az,bx,by,bz)
  %1405 = call float @dx.op.dot3.f32(i32 55, float %100, float %101, float %102, float %1397, float %1398, float %1399)  ; Dot3(ax,ay,az,bx,by,bz)
  %1406 = fmul fast float %1405, 2.000000e+00
  %1407 = fmul fast float %1402, %1402
  %1408 = fsub fast float %1403, %1407
  %1409 = fmul fast float %1406, %1406
  %1410 = fmul fast float %1404, 4.000000e+00
  %1411 = fmul fast float %1410, %1408
  %1412 = fsub fast float %1409, %1411
  %1413 = fcmp fast ult float %1412, 0.000000e+00
  br i1 %1413, label %1424, label %1414, !dx.controlflow.hints !46

; <label>:1414                                    ; preds = %1389
  %1415 = call float @dx.op.unary.f32(i32 24, float %1412)  ; Sqrt(value)
  %1416 = fsub fast float -0.000000e+00, %1406
  %1417 = fsub fast float %1416, %1415
  %1418 = fsub fast float %1415, %1406
  %1419 = fmul fast float %1404, 2.000000e+00
  %1420 = fdiv fast float %1417, %1419
  %1421 = fdiv fast float %1418, %1419
  %1422 = fcmp ogt float %1420, 0.000000e+00
  %1423 = fcmp ogt float %1421, 0.000000e+00
  br label %1424

; <label>:1424                                    ; preds = %1414, %1389
  %1425 = phi i1 [ %1422, %1414 ], [ false, %1389 ]
  %1426 = phi i1 [ %1423, %1414 ], [ false, %1389 ]
  %1427 = or i1 %1425, %1426
  br i1 %1427, label %1467, label %1428

; <label>:1428                                    ; preds = %1424
  %1429 = fmul fast float %1397, %1397
  %1430 = fmul fast float %1398, %1398
  %1431 = fadd fast float %1429, %1430
  %1432 = fmul fast float %1399, %1399
  %1433 = fadd fast float %1431, %1432
  %1434 = call float @dx.op.unary.f32(i32 24, float %1433)  ; Sqrt(value)
  %1435 = fdiv fast float %1397, %1434
  %1436 = fdiv fast float %1398, %1434
  %1437 = fdiv fast float %1399, %1434
  %1438 = call float @dx.op.dot3.f32(i32 55, float %100, float %101, float %102, float %1435, float %1436, float %1437)  ; Dot3(ax,ay,az,bx,by,bz)
  %1439 = fmul fast float %1401, %1401
  %1440 = fsub fast float %1439, %1407
  %1441 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %1440)  ; FMax(a,b)
  %1442 = call float @dx.op.unary.f32(i32 24, float %1441)  ; Sqrt(value)
  %1443 = fmul fast float %1434, %1434
  %1444 = fsub fast float %1443, %1407
  %1445 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %1444)  ; FMax(a,b)
  %1446 = call float @dx.op.unary.f32(i32 24, float %1445)  ; Sqrt(value)
  %1447 = fmul fast float %1438, %1438
  %1448 = fadd fast float %1447, -1.000000e+00
  %1449 = fmul fast float %1443, %1448
  %1450 = fadd fast float %1449, %1439
  %1451 = fmul fast float %1438, %1434
  %1452 = call float @dx.op.unary.f32(i32 24, float %1450)  ; Sqrt(value)
  %1453 = fsub fast float %1452, %1451
  %1454 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %1453)  ; FMax(a,b)
  %1455 = fsub fast float %1401, %1434
  %1456 = fsub fast float %1454, %1455
  %1457 = fsub fast float %1442, %1455
  %1458 = fadd fast float %1457, %1446
  %1459 = fdiv fast float %1456, %1458
  %1460 = fdiv fast float %1446, %1442
  %1461 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %8, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %1462 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %13, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %1463 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %1461, %dx.types.Handle %1462, float %1459, float %1460, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %1464 = extractvalue %dx.types.ResRet.f32 %1463, 0
  %1465 = extractvalue %dx.types.ResRet.f32 %1463, 1
  %1466 = extractvalue %dx.types.ResRet.f32 %1463, 2
  br label %1467

; <label>:1467                                    ; preds = %1428, %1424
  %1468 = phi float [ %1464, %1428 ], [ 0.000000e+00, %1424 ]
  %1469 = phi float [ %1465, %1428 ], [ 0.000000e+00, %1424 ]
  %1470 = phi float [ %1466, %1428 ], [ 0.000000e+00, %1424 ]
  %1471 = fmul fast float %1468, %1384
  %1472 = fmul fast float %1469, %1385
  %1473 = fmul fast float %1470, %1386
  br label %1474

; <label>:1474                                    ; preds = %1467, %1379
  %1475 = phi float [ %1471, %1467 ], [ %1384, %1379 ]
  %1476 = phi float [ %1472, %1467 ], [ %1385, %1379 ]
  %1477 = phi float [ %1473, %1467 ], [ %1386, %1379 ]
  %1478 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 542)  ; CBufferLoadLegacy(handle,regIndex)
  %1479 = extractvalue %dx.types.CBufRet.f32 %1478, 0
  %1480 = extractvalue %dx.types.CBufRet.f32 %1478, 1
  %1481 = extractvalue %dx.types.CBufRet.f32 %1478, 2
  %1482 = extractvalue %dx.types.CBufRet.f32 %1478, 3
  %1483 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 544)  ; CBufferLoadLegacy(handle,regIndex)
  %1484 = extractvalue %dx.types.CBufRet.f32 %1483, 3
  %1485 = extractvalue %dx.types.CBufRet.f32 %1483, 0
  %1486 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 536)  ; CBufferLoadLegacy(handle,regIndex)
  %1487 = extractvalue %dx.types.CBufRet.f32 %1486, 0
  %1488 = extractvalue %dx.types.CBufRet.f32 %1486, 1
  %1489 = extractvalue %dx.types.CBufRet.f32 %1486, 2
  %1490 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 540)  ; CBufferLoadLegacy(handle,regIndex)
  %1491 = extractvalue %dx.types.CBufRet.f32 %1490, 3
  %1492 = extractvalue %dx.types.CBufRet.f32 %1483, 1
  %1493 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %1494 = extractvalue %dx.types.CBufRet.f32 %1493, 2
  %1495 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %1496 = extractvalue %dx.types.CBufRet.f32 %1495, 2
  %1497 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 534)  ; CBufferLoadLegacy(handle,regIndex)
  %1498 = extractvalue %dx.types.CBufRet.f32 %1497, 3
  %1499 = fsub fast float %117, %1492
  %1500 = fadd fast float %1499, %1494
  %1501 = fadd fast float %1500, %1496
  %1502 = fadd fast float %1501, %1498
  %1503 = call float @dx.op.binary.f32(i32 35, float %102, float 0x3E45798EE0000000)  ; FMax(a,b)
  %1504 = fsub fast float 1.000000e+00, %1503
  %1505 = fmul fast float %1504, %1491
  %1506 = fadd fast float %1505, %1503
  %1507 = fdiv fast float %1502, %1506
  %1508 = call float @dx.op.binary.f32(i32 35, float %1507, float 0.000000e+00)  ; FMax(a,b)
  %1509 = fmul fast float %1508, 0xBF847AE140000000
  %1510 = fmul fast float %1487, 0x3FF7154760000000
  %1511 = fmul fast float %1510, %1509
  %1512 = fmul fast float %1511, %1246
  %1513 = fmul fast float %1488, 0x3FF7154760000000
  %1514 = fmul fast float %1513, %1509
  %1515 = fmul fast float %1514, %1246
  %1516 = fmul fast float %1489, 0x3FF7154760000000
  %1517 = fmul fast float %1516, %1509
  %1518 = fmul fast float %1517, %1246
  %1519 = call float @dx.op.unary.f32(i32 21, float %1512)  ; Exp(value)
  %1520 = call float @dx.op.unary.f32(i32 21, float %1515)  ; Exp(value)
  %1521 = call float @dx.op.unary.f32(i32 21, float %1518)  ; Exp(value)
  %1522 = fmul fast float %114, %114
  %1523 = fmul fast float %115, %115
  %1524 = fadd fast float %1522, %1523
  %1525 = fmul fast float %116, %116
  %1526 = fadd fast float %1524, %1525
  %1527 = call float @dx.op.unary.f32(i32 24, float %1526)  ; Sqrt(value)
  %1528 = fmul fast float %1527, %1485
  %1529 = call float @dx.op.unary.f32(i32 7, float %1528)  ; Saturate(value)
  %1530 = fsub fast float %1484, %1482
  %1531 = fmul fast float %1529, %1530
  %1532 = fadd fast float %1531, %1482
  %1533 = fsub fast float %1479, %1519
  %1534 = fsub fast float %1480, %1520
  %1535 = fsub fast float %1481, %1521
  %1536 = fmul fast float %1532, %1533
  %1537 = fmul fast float %1532, %1534
  %1538 = fmul fast float %1532, %1535
  %1539 = fadd fast float %1536, %1519
  %1540 = fadd fast float %1537, %1520
  %1541 = fadd fast float %1538, %1521
  %1542 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 541)  ; CBufferLoadLegacy(handle,regIndex)
  %1543 = extractvalue %dx.types.CBufRet.f32 %1542, 0
  %1544 = extractvalue %dx.types.CBufRet.f32 %1542, 1
  %1545 = extractvalue %dx.types.CBufRet.f32 %1542, 2
  %1546 = extractvalue %dx.types.CBufRet.f32 %1542, 3
  %1547 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 543)  ; CBufferLoadLegacy(handle,regIndex)
  %1548 = extractvalue %dx.types.CBufRet.f32 %1547, 3
  %1549 = extractvalue %dx.types.CBufRet.f32 %1547, 0
  %1550 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 535)  ; CBufferLoadLegacy(handle,regIndex)
  %1551 = extractvalue %dx.types.CBufRet.f32 %1550, 0
  %1552 = extractvalue %dx.types.CBufRet.f32 %1550, 1
  %1553 = extractvalue %dx.types.CBufRet.f32 %1550, 2
  %1554 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 539)  ; CBufferLoadLegacy(handle,regIndex)
  %1555 = extractvalue %dx.types.CBufRet.f32 %1554, 3
  %1556 = extractvalue %dx.types.CBufRet.f32 %1547, 1
  %1557 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 533)  ; CBufferLoadLegacy(handle,regIndex)
  %1558 = extractvalue %dx.types.CBufRet.f32 %1557, 3
  %1559 = fsub fast float %117, %1556
  %1560 = fadd fast float %1559, %1494
  %1561 = fadd fast float %1560, %1496
  %1562 = fadd fast float %1561, %1558
  %1563 = fmul fast float %1504, %1555
  %1564 = fadd fast float %1563, %1503
  %1565 = fdiv fast float %1562, %1564
  %1566 = call float @dx.op.binary.f32(i32 35, float %1565, float 0.000000e+00)  ; FMax(a,b)
  %1567 = fmul fast float %1566, 0xBF847AE140000000
  %1568 = fmul fast float %1551, 0x3FF7154760000000
  %1569 = fmul fast float %1568, %1567
  %1570 = fmul fast float %1569, %1246
  %1571 = fmul fast float %1552, 0x3FF7154760000000
  %1572 = fmul fast float %1571, %1567
  %1573 = fmul fast float %1572, %1246
  %1574 = fmul fast float %1553, 0x3FF7154760000000
  %1575 = fmul fast float %1574, %1567
  %1576 = fmul fast float %1575, %1246
  %1577 = call float @dx.op.unary.f32(i32 21, float %1570)  ; Exp(value)
  %1578 = call float @dx.op.unary.f32(i32 21, float %1573)  ; Exp(value)
  %1579 = call float @dx.op.unary.f32(i32 21, float %1576)  ; Exp(value)
  %1580 = fmul fast float %1527, %1549
  %1581 = call float @dx.op.unary.f32(i32 7, float %1580)  ; Saturate(value)
  %1582 = fsub fast float %1548, %1546
  %1583 = fmul fast float %1581, %1582
  %1584 = fadd fast float %1583, %1546
  %1585 = fsub fast float %1543, %1577
  %1586 = fsub fast float %1544, %1578
  %1587 = fsub fast float %1545, %1579
  %1588 = fmul fast float %1584, %1585
  %1589 = fmul fast float %1584, %1586
  %1590 = fmul fast float %1584, %1587
  %1591 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %22, i32 555)  ; CBufferLoadLegacy(handle,regIndex)
  %1592 = extractvalue %dx.types.CBufRet.f32 %1591, 0
  %1593 = fsub fast float %1577, %1539
  %1594 = fadd fast float %1593, %1588
  %1595 = fsub fast float %1578, %1540
  %1596 = fadd fast float %1595, %1589
  %1597 = fsub fast float %1579, %1541
  %1598 = fadd fast float %1597, %1590
  %1599 = fmul fast float %1594, %1592
  %1600 = fmul fast float %1596, %1592
  %1601 = fmul fast float %1598, %1592
  %1602 = fadd fast float %1599, %1539
  %1603 = fadd fast float %1600, %1540
  %1604 = fadd fast float %1601, %1541
  %1605 = fmul fast float %1602, %1475
  %1606 = fmul fast float %1603, %1476
  %1607 = fmul fast float %1604, %1477
  %1608 = fmul fast float %101, 0xBFDF454580000000
  %1609 = fmul fast float %102, 0x3FDF454580000000
  %1610 = fmul fast float %100, 0xBFDF454580000000
  %1611 = fmul fast float %1605, %1608
  %1612 = fmul fast float %1605, %1609
  %1613 = fmul fast float %1605, %1610
  %1614 = fmul fast float %1606, %1608
  %1615 = fmul fast float %1606, %1609
  %1616 = fmul fast float %1606, %1610
  %1617 = fmul fast float %1607, %1608
  %1618 = fmul fast float %1607, %1609
  %1619 = fmul fast float %1607, %1610
  %1620 = call float @dx.op.dot3.f32(i32 55, float %1611, float %1614, float %1617, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1621 = call float @dx.op.dot3.f32(i32 55, float %1612, float %1615, float %1618, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1622 = call float @dx.op.dot3.f32(i32 55, float %1613, float %1616, float %1619, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1623 = fmul fast float %97, 0x3FD20DD820000000
  %1624 = fmul fast float %1623, %1605
  %1625 = fmul fast float %1623, %1606
  %1626 = fmul fast float %1623, %1607
  %1627 = fmul fast float %1380, %97
  %1628 = fmul fast float %1620, %97
  %1629 = fmul fast float %1621, %97
  %1630 = fmul fast float %1622, %97
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %1624)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %1625)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %1626)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %1627)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %1628)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %1629)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %1630)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.dot4.f32(i32, float, float, float, float, float, float, float, float) #0

; Function Attrs: nounwind
declare void @dx.op.discard(i32, i1) #1

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readnone
declare i32 @dx.op.binary.i32(i32, i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #0

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1) #0

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.viewIdState = !{!28}
!dx.entryPoints = !{!29}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !16, !22}
!5 = !{!6, !8, !9, !11, !12, !13, !14, !15}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{i32 1, %struct.ByteAddressBuffer* undef, !"", i32 0, i32 1, i32 1, i32 11, i32 0, null}
!9 = !{i32 2, %"class.Texture2D<unsigned int>"* undef, !"", i32 0, i32 2, i32 1, i32 2, i32 0, !10}
!10 = !{i32 0, i32 5}
!11 = !{i32 3, %"class.Texture2DArray<unsigned int>"* undef, !"", i32 0, i32 3, i32 1, i32 7, i32 0, !10}
!12 = !{i32 4, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 4, i32 1, i32 2, i32 0, !7}
!13 = !{i32 5, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 5, i32 1, i32 2, i32 0, !7}
!14 = !{i32 6, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 6, i32 1, i32 2, i32 0, !7}
!15 = !{i32 7, %"class.Texture2D<vector<float, 3> >"* undef, !"", i32 0, i32 7, i32 1, i32 2, i32 0, !7}
!16 = !{!17, !18, !19, !20, !21}
!17 = !{i32 0, %"hostlayout.$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 340, null}
!18 = !{i32 1, %hostlayout.View* undef, !"", i32 0, i32 1, i32 1, i32 10076, null}
!19 = !{i32 2, %DeferredLightUniforms* undef, !"", i32 0, i32 2, i32 1, i32 184, null}
!20 = !{i32 3, %VirtualShadowMap* undef, !"", i32 0, i32 3, i32 1, i32 308, null}
!21 = !{i32 4, %Material* undef, !"", i32 0, i32 4, i32 1, i32 156, null}
!22 = !{!23, !24, !25, !26, !27}
!23 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!24 = !{i32 1, %struct.SamplerState* undef, !"", i32 0, i32 1, i32 1, i32 0, null}
!25 = !{i32 2, %struct.SamplerState* undef, !"", i32 0, i32 2, i32 1, i32 0, null}
!26 = !{i32 3, %struct.SamplerState* undef, !"", i32 0, i32 3, i32 1, i32 0, null}
!27 = !{i32 4, %struct.SamplerState* undef, !"", i32 0, i32 4, i32 1, i32 0, null}
!28 = !{[11 x i32] [i32 9, i32 8, i32 127, i32 127, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 127]}
!29 = !{void ()* @InjectMainPS, !"InjectMainPS", !30, !4, !43}
!30 = !{!31, !38, null}
!31 = !{!32, !35, !36}
!32 = !{i32 0, !"TEXCOORD", i8 9, i8 0, !33, i8 4, i32 1, i8 2, i32 0, i8 0, !34}
!33 = !{i32 0}
!34 = !{i32 3, i32 3}
!35 = !{i32 1, !"SV_Position", i8 9, i8 3, !33, i8 4, i32 1, i8 4, i32 1, i8 0, null}
!36 = !{i32 2, !"SV_RenderTargetArrayIndex", i8 5, i8 4, !33, i8 1, i32 1, i8 1, i32 2, i8 0, !37}
!37 = !{i32 3, i32 1}
!38 = !{!39, !41}
!39 = !{i32 0, !"SV_Target", i8 9, i8 16, !33, i8 0, i32 1, i8 4, i32 0, i8 0, !40}
!40 = !{i32 3, i32 15}
!41 = !{i32 1, !"SV_Target", i8 9, i8 16, !42, i8 0, i32 1, i8 4, i32 1, i8 0, !40}
!42 = !{i32 1}
!43 = !{i32 0, i64 16, i32 5, !33}
!44 = distinct !{!44, !"dx.controlflow.hints", i32 1}
!45 = distinct !{!45, !"dx.controlflow.hints", i32 1}
!46 = distinct !{!46, !"dx.controlflow.hints", i32 2}
