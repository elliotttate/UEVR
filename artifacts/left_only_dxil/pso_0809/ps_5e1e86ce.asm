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
; shader debug name: b6af5dfe2fd0bab7b475b2e3b285ee22.pdb
; shader hash: b6af5dfe2fd0bab7b475b2e3b285ee22
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
;   [260 x i8] (type annotation not present)
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
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb0     1
;                                   cbuffer      NA          NA     CB1            cb1     1
;                                   cbuffer      NA          NA     CB2            cb2     1
;                                   cbuffer      NA          NA     CB3            cb3     1
;                                   sampler      NA          NA      S0             s0     1
;                                   sampler      NA          NA      S1             s1     1
;                                   sampler      NA          NA      S2             s2     1
;                                   texture     f32          2d      T0             t0     1
;                                   texture    byte         r/o      T1             t1     1
;                                   texture     u32          2d      T2             t2     1
;                                   texture     u32     2darray      T3             t3     1
;                                   texture     f32          2d      T4             t4     1
;                                   texture     f32          2d      T5             t5     1
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
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
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
%"hostlayout.$Globals" = type { <4 x float>, <4 x float>, i32, [4 x <4 x float>], <4 x float>, i32, float, [4 x <4 x float>], float, i32, i32, [2 x <4 x float>], i32 }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%DeferredLightUniforms = type { <4 x float>, <2 x float>, float, float, float, float, i32, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, <3 x float>, float, <2 x float>, float, float, float, float, <2 x float>, <2 x float>, float, float, i32, i32 }
%VirtualShadowMap = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, <2 x i32>, <2 x i32>, i32, i32, i32, i32, <4 x float>, i32, float, float, float, i32, float, i32, i32, float, float, i32, i32, i32, float, float, float, float, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @InjectMainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 5, i32 5, i32 0, i8 0 }, i32 5, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 4, i32 4, i32 0, i8 0 }, i32 4, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 3 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %8 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 3 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %9 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %10 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 2 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %11 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %12 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %13 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %14 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %10, %dx.types.ResourceProperties { i32 13, i32 308 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %15 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %11, %dx.types.ResourceProperties { i32 13, i32 184 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %16 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %12, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %17 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %13, %dx.types.ResourceProperties { i32 13, i32 260 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %18 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %19 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %20 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !37  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %21 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %17, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %22 = extractvalue %dx.types.CBufRet.i32 %21, 0
  %23 = add i32 %22, 170
  %24 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 %23)  ; CBufferLoadLegacy(handle,regIndex)
  %25 = extractvalue %dx.types.CBufRet.f32 %24, 2
  %26 = uitofp i32 %18 to float
  %27 = fadd float %26, 5.000000e-01
  %28 = add i32 %22, 172
  %29 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 %28)  ; CBufferLoadLegacy(handle,regIndex)
  %30 = extractvalue %dx.types.CBufRet.f32 %29, 3
  %31 = fmul float %27, %30
  %32 = fadd float %25, %31
  %33 = extractvalue %dx.types.CBufRet.f32 %24, 0
  %34 = extractvalue %dx.types.CBufRet.f32 %24, 1
  %35 = extractvalue %dx.types.CBufRet.f32 %29, 0
  %36 = extractvalue %dx.types.CBufRet.f32 %29, 1
  %37 = fdiv float %19, %35
  %38 = fdiv float %20, %36
  %39 = fadd float %33, %37
  %40 = fadd float %34, %38
  %41 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %15, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %42 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %15, i32 9)  ; CBufferLoadLegacy(handle,regIndex)
  %43 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %15, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %44 = extractvalue %dx.types.CBufRet.f32 %43, 0
  %45 = extractvalue %dx.types.CBufRet.f32 %43, 1
  %46 = extractvalue %dx.types.CBufRet.f32 %43, 2
  %47 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %15, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %48 = extractvalue %dx.types.CBufRet.f32 %47, 0
  %49 = extractvalue %dx.types.CBufRet.f32 %47, 1
  %50 = extractvalue %dx.types.CBufRet.f32 %47, 2
  %51 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 14)  ; CBufferLoadLegacy(handle,regIndex)
  %52 = extractvalue %dx.types.CBufRet.f32 %51, 0
  %53 = extractvalue %dx.types.CBufRet.f32 %51, 1
  %54 = extractvalue %dx.types.CBufRet.f32 %51, 2
  %55 = extractvalue %dx.types.CBufRet.f32 %51, 3
  %56 = call float @dx.op.dot4.f32(i32 56, float %52, float %53, float %54, float %55, float %39, float %40, float %32, float 1.000000e+00)  ; Dot4(ax,ay,az,aw,bx,by,bz,bw)
  %57 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 15)  ; CBufferLoadLegacy(handle,regIndex)
  %58 = extractvalue %dx.types.CBufRet.f32 %57, 0
  %59 = extractvalue %dx.types.CBufRet.f32 %57, 1
  %60 = extractvalue %dx.types.CBufRet.f32 %57, 2
  %61 = extractvalue %dx.types.CBufRet.f32 %57, 3
  %62 = call float @dx.op.dot4.f32(i32 56, float %58, float %59, float %60, float %61, float %39, float %40, float %32, float 1.000000e+00)  ; Dot4(ax,ay,az,aw,bx,by,bz,bw)
  %63 = fcmp ogt float %56, -0.000000e+00
  call void @dx.op.discard(i32 82, i1 %63)  ; Discard(condition)
  %64 = fcmp ogt float %62, -0.000000e+00
  call void @dx.op.discard(i32 82, i1 %64)  ; Discard(condition)
  %65 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %66 = extractvalue %dx.types.CBufRet.f32 %65, 0
  %67 = fmul fast float %56, %66
  %68 = fsub fast float -0.000000e+00, %67
  %69 = call float @dx.op.unary.f32(i32 7, float %68)  ; Saturate(value)
  %70 = extractvalue %dx.types.CBufRet.f32 %65, 1
  %71 = fmul fast float %62, %70
  %72 = fsub fast float -0.000000e+00, %71
  %73 = call float @dx.op.unary.f32(i32 7, float %72)  ; Saturate(value)
  %74 = fmul fast float %73, %69
  %75 = call float @dx.op.dot3.f32(i32 55, float %48, float %49, float %50, float %48, float %49, float %50), !dx.precise !37  ; Dot3(ax,ay,az,bx,by,bz)
  %76 = call float @dx.op.unary.f32(i32 25, float %75), !dx.precise !37  ; Rsqrt(value)
  %77 = fmul float %48, %76
  %78 = fmul float %49, %76
  %79 = fmul float %50, %76
  %80 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %17, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %81 = extractvalue %dx.types.CBufRet.i32 %80, 0
  %82 = add i32 %81, 172
  %83 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 %82)  ; CBufferLoadLegacy(handle,regIndex)
  %84 = extractvalue %dx.types.CBufRet.f32 %83, 3
  %85 = fmul float %77, -5.000000e-01
  %86 = fmul float %78, -5.000000e-01
  %87 = fmul float %79, -5.000000e-01
  %88 = fmul float %85, %84
  %89 = fmul float %86, %84
  %90 = fmul float %87, %84
  %91 = fadd float %39, %88
  %92 = fadd float %40, %89
  %93 = fadd float %32, %90
  %94 = fsub fast float -0.000000e+00, %93
  %95 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %17, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %96 = extractvalue %dx.types.CBufRet.i32 %95, 0
  %97 = icmp eq i32 %96, 0
  br i1 %97, label %261, label %98, !dx.controlflow.hints !39

; <label>:98                                      ; preds = %0
  %99 = extractvalue %dx.types.CBufRet.f32 %41, 0
  %100 = fcmp fast ogt float %99, -2.000000e+00
  %101 = extractvalue %dx.types.CBufRet.f32 %42, 1
  %102 = fcmp ule float %101, -2.000000e+00
  %103 = and i1 %100, %102
  br i1 %103, label %151, label %104, !dx.controlflow.hints !40

; <label>:104                                     ; preds = %98
  %105 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %15, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %106 = extractvalue %dx.types.CBufRet.f32 %105, 0
  %107 = extractvalue %dx.types.CBufRet.f32 %105, 1
  %108 = extractvalue %dx.types.CBufRet.f32 %105, 2
  %109 = fsub fast float %91, %106
  %110 = fsub fast float %92, %107
  %111 = fsub fast float %93, %108
  %112 = fmul fast float %109, %109
  %113 = fmul fast float %110, %110
  %114 = fadd fast float %112, %113
  %115 = fmul fast float %111, %111
  %116 = fadd fast float %114, %115
  %117 = call float @dx.op.unary.f32(i32 24, float %116)  ; Sqrt(value)
  %118 = fdiv fast float %109, %117
  %119 = fdiv fast float %110, %117
  %120 = fdiv fast float %111, %117
  %121 = fdiv fast float %119, %118
  %122 = call float @dx.op.unary.f32(i32 17, float %121)  ; Atan(value)
  %123 = fadd fast float %122, 0x400921FB60000000
  %124 = fadd fast float %122, 0xC00921FB60000000
  %125 = fcmp fast olt float %118, 0.000000e+00
  %126 = fcmp fast oeq float %118, 0.000000e+00
  %127 = fcmp fast oge float %119, 0.000000e+00
  %128 = fcmp fast olt float %119, 0.000000e+00
  %129 = and i1 %125, %127
  %130 = select i1 %129, float %123, float %122
  %131 = and i1 %125, %128
  %132 = select i1 %131, float %124, float %130
  %133 = and i1 %126, %128
  %134 = and i1 %126, %127
  %135 = fmul fast float %132, 0x3FC45F3060000000
  %136 = select i1 %133, float -2.500000e-01, float %135
  %137 = select i1 %134, float 2.500000e-01, float %136
  %138 = fcmp fast ogt float %137, 0.000000e+00
  %139 = fadd fast float %137, 1.000000e+00
  %140 = select i1 %138, float %137, float %139
  %141 = call float @dx.op.unary.f32(i32 15, float %120)  ; Acos(value)
  %142 = fmul fast float %141, 0x3FD45F3060000000
  %143 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %144 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %8, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %145 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %143, %dx.types.Handle %144, float %140, float %142, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %146 = extractvalue %dx.types.ResRet.f32 %145, 0
  %147 = extractvalue %dx.types.CBufRet.f32 %105, 3
  %148 = fmul fast float %147, %117
  %149 = fcmp fast olt float %148, %146
  %150 = uitofp i1 %149 to float
  br label %261

; <label>:151                                     ; preds = %98
  %152 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %153 = extractvalue %dx.types.CBufRet.f32 %152, 0
  %154 = extractvalue %dx.types.CBufRet.f32 %152, 1
  %155 = extractvalue %dx.types.CBufRet.f32 %152, 3
  %156 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %157 = extractvalue %dx.types.CBufRet.f32 %156, 0
  %158 = extractvalue %dx.types.CBufRet.f32 %156, 1
  %159 = extractvalue %dx.types.CBufRet.f32 %156, 3
  %160 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %161 = extractvalue %dx.types.CBufRet.f32 %160, 0
  %162 = extractvalue %dx.types.CBufRet.f32 %160, 1
  %163 = extractvalue %dx.types.CBufRet.f32 %160, 3
  %164 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 6)  ; CBufferLoadLegacy(handle,regIndex)
  %165 = extractvalue %dx.types.CBufRet.f32 %164, 0
  %166 = extractvalue %dx.types.CBufRet.f32 %164, 1
  %167 = extractvalue %dx.types.CBufRet.f32 %164, 3
  %168 = fmul fast float %153, %91
  %169 = call float @dx.op.tertiary.f32(i32 46, float %92, float %157, float %168)  ; FMad(a,b,c)
  %170 = call float @dx.op.tertiary.f32(i32 46, float %93, float %161, float %169)  ; FMad(a,b,c)
  %171 = fadd fast float %170, %165
  %172 = fmul fast float %154, %91
  %173 = call float @dx.op.tertiary.f32(i32 46, float %92, float %158, float %172)  ; FMad(a,b,c)
  %174 = call float @dx.op.tertiary.f32(i32 46, float %93, float %162, float %173)  ; FMad(a,b,c)
  %175 = fadd fast float %174, %166
  %176 = fmul fast float %155, %91
  %177 = call float @dx.op.tertiary.f32(i32 46, float %92, float %159, float %176)  ; FMad(a,b,c)
  %178 = call float @dx.op.tertiary.f32(i32 46, float %93, float %163, float %177)  ; FMad(a,b,c)
  %179 = fadd fast float %178, %167
  %180 = fdiv fast float %171, %179
  %181 = fdiv fast float %175, %179
  %182 = fcmp fast ole float %180, 1.000000e+00
  %183 = fcmp fast ole float %181, 1.000000e+00
  %184 = fcmp fast oge float %180, 0.000000e+00
  %185 = fcmp fast oge float %181, 0.000000e+00
  %186 = and i1 %184, %182
  %187 = and i1 %185, %183
  %188 = and i1 %186, %187
  br i1 %188, label %189, label %261

; <label>:189                                     ; preds = %151
  %190 = extractvalue %dx.types.CBufRet.f32 %164, 2
  %191 = extractvalue %dx.types.CBufRet.f32 %160, 2
  %192 = extractvalue %dx.types.CBufRet.f32 %156, 2
  %193 = extractvalue %dx.types.CBufRet.f32 %152, 2
  %194 = fmul fast float %193, %91
  %195 = call float @dx.op.tertiary.f32(i32 46, float %92, float %192, float %194)  ; FMad(a,b,c)
  %196 = call float @dx.op.tertiary.f32(i32 46, float %93, float %191, float %195)  ; FMad(a,b,c)
  %197 = fadd fast float %196, %190
  %198 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 7)  ; CBufferLoadLegacy(handle,regIndex)
  %199 = extractvalue %dx.types.CBufRet.f32 %198, 0
  %200 = extractvalue %dx.types.CBufRet.f32 %198, 1
  %201 = extractvalue %dx.types.CBufRet.f32 %198, 2
  %202 = extractvalue %dx.types.CBufRet.f32 %198, 3
  %203 = fmul fast float %199, %180
  %204 = fmul fast float %200, %181
  %205 = fadd fast float %203, -5.000000e-01
  %206 = fadd fast float %204, -5.000000e-01
  %207 = call float @dx.op.unary.f32(i32 22, float %205)  ; Frc(value)
  %208 = call float @dx.op.unary.f32(i32 22, float %206)  ; Frc(value)
  %209 = call float @dx.op.unary.f32(i32 27, float %205)  ; Round_ni(value)
  %210 = call float @dx.op.unary.f32(i32 27, float %206)  ; Round_ni(value)
  %211 = fadd fast float %209, 1.000000e+00
  %212 = fadd fast float %210, 1.000000e+00
  %213 = fmul fast float %211, %201
  %214 = fmul fast float %212, %202
  %215 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %216 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %8, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %217 = call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73, %dx.types.Handle %215, %dx.types.Handle %216, float %213, float %214, float undef, float undef, i32 0, i32 0, i32 0)  ; TextureGather(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,channel)
  %218 = extractvalue %dx.types.ResRet.f32 %217, 0
  %219 = extractvalue %dx.types.ResRet.f32 %217, 1
  %220 = extractvalue %dx.types.ResRet.f32 %217, 2
  %221 = extractvalue %dx.types.ResRet.f32 %217, 3
  %222 = fmul fast float %197, 4.000000e+01
  %223 = fadd fast float %222, -1.000000e+00
  %224 = fmul fast float %218, 4.000000e+01
  %225 = fmul fast float %219, 4.000000e+01
  %226 = fmul fast float %220, 4.000000e+01
  %227 = fmul fast float %221, 4.000000e+01
  %228 = fsub fast float %224, %223
  %229 = fsub fast float %225, %223
  %230 = fsub fast float %226, %223
  %231 = fsub fast float %227, %223
  %232 = call float @dx.op.unary.f32(i32 7, float %228)  ; Saturate(value)
  %233 = call float @dx.op.unary.f32(i32 7, float %229)  ; Saturate(value)
  %234 = call float @dx.op.unary.f32(i32 7, float %230)  ; Saturate(value)
  %235 = call float @dx.op.unary.f32(i32 7, float %231)  ; Saturate(value)
  %236 = fcmp fast ogt float %218, 0x3FEFAE1480000000
  %237 = fcmp fast ogt float %219, 0x3FEFAE1480000000
  %238 = fcmp fast ogt float %220, 0x3FEFAE1480000000
  %239 = fcmp fast ogt float %221, 0x3FEFAE1480000000
  %240 = uitofp i1 %236 to float
  %241 = uitofp i1 %237 to float
  %242 = uitofp i1 %238 to float
  %243 = uitofp i1 %239 to float
  %244 = fadd fast float %240, %232
  %245 = fadd fast float %241, %233
  %246 = fadd fast float %234, %242
  %247 = fadd fast float %235, %243
  %248 = call float @dx.op.unary.f32(i32 7, float %244)  ; Saturate(value)
  %249 = call float @dx.op.unary.f32(i32 7, float %245)  ; Saturate(value)
  %250 = call float @dx.op.unary.f32(i32 7, float %246)  ; Saturate(value)
  %251 = call float @dx.op.unary.f32(i32 7, float %247)  ; Saturate(value)
  %252 = fsub fast float %250, %251
  %253 = fsub fast float %249, %248
  %254 = fmul fast float %252, %207
  %255 = fmul fast float %253, %207
  %256 = fadd fast float %254, %251
  %257 = fadd fast float %255, %248
  %258 = fsub fast float %257, %256
  %259 = fmul fast float %258, %208
  %260 = fadd fast float %259, %256
  br label %261

; <label>:261                                     ; preds = %189, %151, %104, %0
  %262 = phi float [ %150, %104 ], [ %260, %189 ], [ 1.000000e+00, %151 ], [ 1.000000e+00, %0 ]
  %263 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %17, i32 16)  ; CBufferLoadLegacy(handle,regIndex)
  %264 = extractvalue %dx.types.CBufRet.i32 %263, 0
  %265 = mul i32 %264, 288
  %266 = or i32 %265, 16
  %267 = add i32 %266, 188
  %268 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %269 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %267, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %270 = extractvalue %dx.types.ResRet.i32 %269, 0
  %271 = icmp eq i32 %270, 0
  %272 = add i32 %266, 192
  %273 = add i32 %266, 208
  br i1 %271, label %274, label %581

; <label>:274                                     ; preds = %261
  %275 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %272, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %276 = extractvalue %dx.types.ResRet.i32 %275, 0
  %277 = extractvalue %dx.types.ResRet.i32 %275, 1
  %278 = extractvalue %dx.types.ResRet.i32 %275, 2
  %279 = bitcast i32 %276 to float
  %280 = bitcast i32 %277 to float
  %281 = bitcast i32 %278 to float
  %282 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %273, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %283 = extractvalue %dx.types.ResRet.i32 %282, 0
  %284 = extractvalue %dx.types.ResRet.i32 %282, 1
  %285 = extractvalue %dx.types.ResRet.i32 %282, 2
  %286 = add i32 %266, 220
  %287 = bitcast i32 %283 to float
  %288 = bitcast i32 %284 to float
  %289 = bitcast i32 %285 to float
  %290 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %286, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %291 = extractvalue %dx.types.ResRet.i32 %290, 0
  %292 = add i32 %266, 224
  %293 = bitcast i32 %291 to float
  %294 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %292, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %295 = extractvalue %dx.types.ResRet.i32 %294, 0
  %296 = extractvalue %dx.types.ResRet.i32 %294, 1
  %297 = extractvalue %dx.types.ResRet.i32 %294, 2
  %298 = bitcast i32 %295 to float
  %299 = bitcast i32 %296 to float
  %300 = bitcast i32 %297 to float
  %301 = add i32 %266, 248
  %302 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %301, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %303 = extractvalue %dx.types.ResRet.i32 %302, 0
  %304 = icmp eq i32 %303, -1
  br i1 %304, label %309, label %305

; <label>:305                                     ; preds = %274
  %306 = lshr i32 %303, 16
  %307 = add nsw i32 %306, -1024
  %308 = and i32 %303, 65535
  br label %309

; <label>:309                                     ; preds = %305, %274
  %310 = phi i32 [ %307, %305 ], [ 1024, %274 ]
  %311 = phi i32 [ %308, %305 ], [ -1, %274 ]
  %312 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %313 = extractvalue %dx.types.CBufRet.f32 %312, 0
  %314 = extractvalue %dx.types.CBufRet.f32 %312, 1
  %315 = extractvalue %dx.types.CBufRet.f32 %312, 2
  %316 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %317 = extractvalue %dx.types.CBufRet.f32 %316, 0
  %318 = extractvalue %dx.types.CBufRet.f32 %316, 1
  %319 = extractvalue %dx.types.CBufRet.f32 %316, 2
  %320 = fsub float %279, %313
  %321 = fsub float %280, %314
  %322 = fsub float %281, %315
  %323 = fsub float %287, %317
  %324 = fsub float %288, %318
  %325 = fsub float %289, %319
  %326 = fadd float %320, %323
  %327 = fadd float %321, %324
  %328 = fadd float %322, %325
  %329 = fadd float %298, %326
  %330 = fadd float %299, %327
  %331 = fadd float %300, %328
  %332 = fadd float %91, %329
  %333 = fadd float %92, %330
  %334 = fadd float %93, %331
  %335 = fmul float %332, %332
  %336 = fmul float %333, %333
  %337 = fadd float %335, %336
  %338 = fmul float %334, %334
  %339 = fadd float %338, %337
  %340 = call float @dx.op.unary.f32(i32 24, float %339), !dx.precise !37  ; Sqrt(value)
  %341 = call float @dx.op.unary.f32(i32 23, float %340), !dx.precise !37  ; Log(value)
  %342 = fadd float %293, %341
  %343 = call float @dx.op.unary.f32(i32 27, float %342), !dx.precise !37  ; Round_ni(value)
  %344 = fptosi float %343 to i32
  %345 = sub nsw i32 %344, %310
  %346 = call i32 @dx.op.binary.i32(i32 37, i32 0, i32 %345)  ; IMax(a,b)
  %347 = icmp slt i32 %346, %311
  br i1 %347, label %348, label %820

; <label>:348                                     ; preds = %309
  %349 = add nsw i32 %346, %264
  %350 = mul i32 %349, 288
  %351 = or i32 %350, 16
  %352 = add i32 %351, 48
  %353 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %352, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %354 = extractvalue %dx.types.ResRet.i32 %353, 0
  %355 = extractvalue %dx.types.ResRet.i32 %353, 1
  %356 = extractvalue %dx.types.ResRet.i32 %353, 2
  %357 = add i32 %351, 64
  %358 = bitcast i32 %354 to float
  %359 = bitcast i32 %355 to float
  %360 = bitcast i32 %356 to float
  %361 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %357, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %362 = extractvalue %dx.types.ResRet.i32 %361, 0
  %363 = extractvalue %dx.types.ResRet.i32 %361, 1
  %364 = extractvalue %dx.types.ResRet.i32 %361, 2
  %365 = add i32 %351, 80
  %366 = bitcast i32 %362 to float
  %367 = bitcast i32 %363 to float
  %368 = bitcast i32 %364 to float
  %369 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %365, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %370 = extractvalue %dx.types.ResRet.i32 %369, 0
  %371 = extractvalue %dx.types.ResRet.i32 %369, 1
  %372 = extractvalue %dx.types.ResRet.i32 %369, 2
  %373 = add i32 %351, 96
  %374 = bitcast i32 %370 to float
  %375 = bitcast i32 %371 to float
  %376 = bitcast i32 %372 to float
  %377 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %373, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %378 = extractvalue %dx.types.ResRet.i32 %377, 0
  %379 = extractvalue %dx.types.ResRet.i32 %377, 1
  %380 = extractvalue %dx.types.ResRet.i32 %377, 2
  %381 = bitcast i32 %378 to float
  %382 = bitcast i32 %379 to float
  %383 = bitcast i32 %380 to float
  %384 = add i32 %351, 192
  %385 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %384, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %386 = extractvalue %dx.types.ResRet.i32 %385, 0
  %387 = extractvalue %dx.types.ResRet.i32 %385, 1
  %388 = extractvalue %dx.types.ResRet.i32 %385, 2
  %389 = bitcast i32 %386 to float
  %390 = bitcast i32 %387 to float
  %391 = bitcast i32 %388 to float
  %392 = add i32 %351, 208
  %393 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %392, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %394 = extractvalue %dx.types.ResRet.i32 %393, 0
  %395 = extractvalue %dx.types.ResRet.i32 %393, 1
  %396 = extractvalue %dx.types.ResRet.i32 %393, 2
  %397 = bitcast i32 %394 to float
  %398 = bitcast i32 %395 to float
  %399 = bitcast i32 %396 to float
  %400 = fsub float %389, %313
  %401 = fsub float %390, %314
  %402 = fsub float %391, %315
  %403 = fsub float %397, %317
  %404 = fsub float %398, %318
  %405 = fsub float %399, %319
  %406 = fadd float %400, %403
  %407 = fadd float %401, %404
  %408 = fadd float %402, %405
  %409 = fadd fast float %406, %91
  %410 = fadd fast float %407, %92
  %411 = fadd fast float %408, %93
  %412 = fmul fast float %409, %358
  %413 = call float @dx.op.tertiary.f32(i32 46, float %410, float %366, float %412)  ; FMad(a,b,c)
  %414 = call float @dx.op.tertiary.f32(i32 46, float %411, float %374, float %413)  ; FMad(a,b,c)
  %415 = fadd fast float %414, %381
  %416 = fmul fast float %409, %359
  %417 = call float @dx.op.tertiary.f32(i32 46, float %410, float %367, float %416)  ; FMad(a,b,c)
  %418 = call float @dx.op.tertiary.f32(i32 46, float %411, float %375, float %417)  ; FMad(a,b,c)
  %419 = fadd fast float %418, %382
  %420 = fmul fast float %409, %360
  %421 = call float @dx.op.tertiary.f32(i32 46, float %410, float %368, float %420)  ; FMad(a,b,c)
  %422 = call float @dx.op.tertiary.f32(i32 46, float %411, float %376, float %421)  ; FMad(a,b,c)
  %423 = fadd fast float %422, %383
  %424 = fmul fast float %415, 1.280000e+02
  %425 = fmul fast float %419, 1.280000e+02
  %426 = fptoui float %424 to i32
  %427 = fptoui float %425 to i32
  %428 = icmp slt i32 %349, 8192
  br i1 %428, label %429, label %432

; <label>:429                                     ; preds = %348
  %430 = lshr i32 %349, 7
  %431 = and i32 %349, 127
  br label %442

; <label>:432                                     ; preds = %348
  %433 = add i32 %349, -8191
  %434 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %14, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %435 = extractvalue %dx.types.CBufRet.i32 %434, 0
  %436 = and i32 %435, 31
  %437 = lshr i32 %433, %436
  %438 = mul i32 %437, 192
  %439 = extractvalue %dx.types.CBufRet.i32 %434, 1
  %440 = and i32 %439, %433
  %441 = shl i32 %440, 7
  br label %442

; <label>:442                                     ; preds = %432, %429
  %443 = phi i32 [ %431, %429 ], [ %441, %432 ]
  %444 = phi i32 [ %430, %429 ], [ %438, %432 ]
  %445 = select i1 %428, i32 0, i32 %426
  %446 = add i32 %443, %445
  %447 = select i1 %428, i32 0, i32 %427
  %448 = add i32 %444, %447
  %449 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 2, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2D<U32>
  %450 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %449, i32 0, i32 %446, i32 %448, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %451 = extractvalue %dx.types.ResRet.i32 %450, 0
  %452 = lshr i32 %451, 20
  %453 = and i32 %452, 63
  %454 = icmp slt i32 %451, 0
  br i1 %454, label %455, label %575

; <label>:455                                     ; preds = %442
  %456 = icmp eq i32 %453, 0
  %457 = zext i1 %456 to i32
  %458 = add i32 %453, %349
  %459 = fmul fast float %415, 1.638400e+04
  %460 = fmul fast float %419, 1.638400e+04
  %461 = fptoui float %459 to i32
  %462 = fptoui float %460 to i32
  br i1 %456, label %552, label %463

; <label>:463                                     ; preds = %455
  %464 = add i32 %351, 240
  %465 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %464, i32 undef, i8 3, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %466 = extractvalue %dx.types.ResRet.i32 %465, 0
  %467 = extractvalue %dx.types.ResRet.i32 %465, 1
  %468 = mul i32 %458, 288
  %469 = or i32 %468, 16
  %470 = add i32 %469, 240
  %471 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %470, i32 undef, i8 3, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %472 = extractvalue %dx.types.ResRet.i32 %471, 0
  %473 = extractvalue %dx.types.ResRet.i32 %471, 1
  %474 = shl i32 %466, 5
  %475 = shl i32 %467, 5
  %476 = shl i32 %472, 5
  %477 = shl i32 %473, 5
  %478 = sub i32 %426, %474
  %479 = sub i32 %427, %475
  %480 = and i32 %452, 31
  %481 = shl i32 %476, %480
  %482 = shl i32 %477, %480
  %483 = add i32 %478, %481
  %484 = add i32 %479, %482
  %485 = lshr i32 %483, %480
  %486 = lshr i32 %484, %480
  %487 = shl i32 %485, 7
  %488 = shl i32 %486, 7
  %489 = or i32 %487, 127
  %490 = or i32 %488, 127
  %491 = add i32 %351, 32
  %492 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %491, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %493 = extractvalue %dx.types.ResRet.i32 %492, 2
  %494 = bitcast i32 %493 to float
  %495 = add i32 %469, 32
  %496 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %495, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %497 = extractvalue %dx.types.ResRet.i32 %496, 2
  %498 = bitcast i32 %497 to float
  %499 = sitofp i32 %466 to float
  %500 = sitofp i32 %467 to float
  %501 = sitofp i32 %472 to float
  %502 = sitofp i32 %473 to float
  %503 = shl i32 1, %480
  %504 = uitofp i32 %503 to float
  %505 = fdiv fast float 1.000000e+00, %504
  %506 = fmul fast float %505, %499
  %507 = fmul fast float %505, %500
  %508 = fsub fast float %501, %506
  %509 = fsub fast float %502, %507
  %510 = fmul fast float %508, 2.500000e-01
  %511 = fmul fast float %509, 2.500000e-01
  %512 = fmul fast float %505, %494
  %513 = fsub fast float %498, %512
  %514 = fmul fast float %505, %415
  %515 = fmul fast float %505, %419
  %516 = fadd fast float %510, %514
  %517 = fadd fast float %511, %515
  %518 = fmul fast float %516, 1.638400e+04
  %519 = fmul fast float %517, 1.638400e+04
  %520 = fptoui float %518 to i32
  %521 = fptoui float %519 to i32
  %522 = call i32 @dx.op.binary.i32(i32 39, i32 %520, i32 %487)  ; UMax(a,b)
  %523 = call i32 @dx.op.binary.i32(i32 39, i32 %521, i32 %488)  ; UMax(a,b)
  %524 = call i32 @dx.op.binary.i32(i32 40, i32 %522, i32 %489)  ; UMin(a,b)
  %525 = call i32 @dx.op.binary.i32(i32 40, i32 %523, i32 %490)  ; UMin(a,b)
  %526 = icmp slt i32 %458, 8192
  br i1 %526, label %527, label %530

; <label>:527                                     ; preds = %463
  %528 = lshr i32 %458, 7
  %529 = and i32 %458, 127
  br label %540

; <label>:530                                     ; preds = %463
  %531 = add i32 %458, -8191
  %532 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %14, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %533 = extractvalue %dx.types.CBufRet.i32 %532, 0
  %534 = and i32 %533, 31
  %535 = lshr i32 %531, %534
  %536 = mul i32 %535, 192
  %537 = extractvalue %dx.types.CBufRet.i32 %532, 1
  %538 = and i32 %537, %531
  %539 = shl i32 %538, 7
  br label %540

; <label>:540                                     ; preds = %530, %527
  %541 = phi i32 [ %529, %527 ], [ %539, %530 ]
  %542 = phi i32 [ %528, %527 ], [ %536, %530 ]
  %543 = select i1 %526, i32 0, i32 %485
  %544 = add i32 %541, %543
  %545 = select i1 %526, i32 0, i32 %486
  %546 = add i32 %542, %545
  %547 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %449, i32 0, i32 %544, i32 %546, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %548 = extractvalue %dx.types.ResRet.i32 %547, 0
  %549 = and i32 %548, -2081423360
  %550 = icmp eq i32 %549, -2147483648
  %551 = zext i1 %550 to i32
  br label %552

; <label>:552                                     ; preds = %540, %455
  %553 = phi i32 [ %524, %540 ], [ %461, %455 ]
  %554 = phi i32 [ %525, %540 ], [ %462, %455 ]
  %555 = phi i32 [ %551, %540 ], [ %457, %455 ]
  %556 = phi i32 [ %548, %540 ], [ %451, %455 ]
  %557 = phi float [ %505, %540 ], [ 1.000000e+00, %455 ]
  %558 = phi float [ %513, %540 ], [ 0.000000e+00, %455 ]
  %559 = icmp eq i32 %555, 0
  br i1 %559, label %575, label %560

; <label>:560                                     ; preds = %552
  %561 = shl i32 %556, 7
  %562 = and i32 %561, 130944
  %563 = lshr i32 %556, 3
  %564 = and i32 %563, 130944
  %565 = and i32 %553, 127
  %566 = and i32 %554, 127
  %567 = or i32 %562, %565
  %568 = or i32 %564, %566
  %569 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 7, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2DArray<U32>
  %570 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %569, i32 0, i32 %567, i32 %568, i32 0, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %571 = extractvalue %dx.types.ResRet.i32 %570, 0
  %572 = bitcast i32 %571 to float
  %573 = fsub fast float %572, %558
  %574 = fdiv fast float %573, %557
  br label %575

; <label>:575                                     ; preds = %560, %552, %442
  %576 = phi i1 [ true, %560 ], [ false, %552 ], [ false, %442 ]
  %577 = phi float [ %574, %560 ], [ 0.000000e+00, %552 ], [ 0.000000e+00, %442 ]
  %578 = fcmp fast ogt float %577, %423
  %579 = and i1 %576, %578
  %580 = select i1 %579, float 0.000000e+00, float 1.000000e+00
  br label %820

; <label>:581                                     ; preds = %261
  %582 = add i32 %266, 48
  %583 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %582, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %584 = extractvalue %dx.types.ResRet.i32 %583, 0
  %585 = extractvalue %dx.types.ResRet.i32 %583, 1
  %586 = extractvalue %dx.types.ResRet.i32 %583, 2
  %587 = extractvalue %dx.types.ResRet.i32 %583, 3
  %588 = add i32 %266, 64
  %589 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %588, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %590 = extractvalue %dx.types.ResRet.i32 %589, 0
  %591 = extractvalue %dx.types.ResRet.i32 %589, 1
  %592 = extractvalue %dx.types.ResRet.i32 %589, 2
  %593 = extractvalue %dx.types.ResRet.i32 %589, 3
  %594 = add i32 %266, 80
  %595 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %594, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %596 = extractvalue %dx.types.ResRet.i32 %595, 0
  %597 = extractvalue %dx.types.ResRet.i32 %595, 1
  %598 = extractvalue %dx.types.ResRet.i32 %595, 2
  %599 = extractvalue %dx.types.ResRet.i32 %595, 3
  %600 = add i32 %266, 96
  %601 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %600, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %602 = extractvalue %dx.types.ResRet.i32 %601, 0
  %603 = extractvalue %dx.types.ResRet.i32 %601, 1
  %604 = extractvalue %dx.types.ResRet.i32 %601, 2
  %605 = extractvalue %dx.types.ResRet.i32 %601, 3
  %606 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %272, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %607 = extractvalue %dx.types.ResRet.i32 %606, 0
  %608 = extractvalue %dx.types.ResRet.i32 %606, 1
  %609 = extractvalue %dx.types.ResRet.i32 %606, 2
  %610 = bitcast i32 %607 to float
  %611 = bitcast i32 %608 to float
  %612 = bitcast i32 %609 to float
  %613 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %273, i32 undef, i8 7, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %614 = extractvalue %dx.types.ResRet.i32 %613, 0
  %615 = extractvalue %dx.types.ResRet.i32 %613, 1
  %616 = extractvalue %dx.types.ResRet.i32 %613, 2
  %617 = bitcast i32 %614 to float
  %618 = bitcast i32 %615 to float
  %619 = bitcast i32 %616 to float
  %620 = add i32 %266, 268
  %621 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %620, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %622 = extractvalue %dx.types.ResRet.i32 %621, 0
  %623 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %624 = extractvalue %dx.types.CBufRet.f32 %623, 0
  %625 = extractvalue %dx.types.CBufRet.f32 %623, 1
  %626 = extractvalue %dx.types.CBufRet.f32 %623, 2
  %627 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %628 = extractvalue %dx.types.CBufRet.f32 %627, 0
  %629 = extractvalue %dx.types.CBufRet.f32 %627, 1
  %630 = extractvalue %dx.types.CBufRet.f32 %627, 2
  %631 = fsub float %610, %624
  %632 = fsub float %611, %625
  %633 = fsub float %612, %626
  %634 = fsub float %617, %628
  %635 = fsub float %618, %629
  %636 = fsub float %619, %630
  %637 = fadd float %631, %634
  %638 = fadd float %632, %635
  %639 = fadd float %633, %636
  %640 = fadd fast float %637, %91
  %641 = fadd fast float %638, %92
  %642 = fadd fast float %639, %93
  %643 = icmp eq i32 %270, 2
  br i1 %643, label %694, label %644

; <label>:644                                     ; preds = %581
  %645 = call float @dx.op.unary.f32(i32 6, float %640)  ; FAbs(value)
  %646 = call float @dx.op.unary.f32(i32 6, float %641)  ; FAbs(value)
  %647 = fcmp fast oge float %645, %646
  %648 = call float @dx.op.unary.f32(i32 6, float %642)  ; FAbs(value)
  %649 = fcmp fast oge float %645, %648
  %650 = and i1 %647, %649
  br i1 %650, label %651, label %654

; <label>:651                                     ; preds = %644
  %652 = fcmp ule float %640, 0.000000e+00
  %653 = zext i1 %652 to i32
  br label %662

; <label>:654                                     ; preds = %644
  %655 = fcmp fast ogt float %646, %648
  br i1 %655, label %656, label %659

; <label>:656                                     ; preds = %654
  %657 = fcmp fast ogt float %641, 0.000000e+00
  %658 = select i1 %657, i32 2, i32 3
  br label %662

; <label>:659                                     ; preds = %654
  %660 = fcmp fast ogt float %642, 0.000000e+00
  %661 = select i1 %660, i32 4, i32 5
  br label %662

; <label>:662                                     ; preds = %659, %656, %651
  %663 = phi i32 [ %653, %651 ], [ %658, %656 ], [ %661, %659 ]
  %664 = add i32 %663, %264
  %665 = mul i32 %664, 288
  %666 = or i32 %665, 16
  %667 = add i32 %666, 48
  %668 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %667, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %669 = extractvalue %dx.types.ResRet.i32 %668, 0
  %670 = extractvalue %dx.types.ResRet.i32 %668, 1
  %671 = extractvalue %dx.types.ResRet.i32 %668, 2
  %672 = extractvalue %dx.types.ResRet.i32 %668, 3
  %673 = add i32 %666, 64
  %674 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %673, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %675 = extractvalue %dx.types.ResRet.i32 %674, 0
  %676 = extractvalue %dx.types.ResRet.i32 %674, 1
  %677 = extractvalue %dx.types.ResRet.i32 %674, 2
  %678 = extractvalue %dx.types.ResRet.i32 %674, 3
  %679 = add i32 %666, 80
  %680 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %679, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %681 = extractvalue %dx.types.ResRet.i32 %680, 0
  %682 = extractvalue %dx.types.ResRet.i32 %680, 1
  %683 = extractvalue %dx.types.ResRet.i32 %680, 2
  %684 = extractvalue %dx.types.ResRet.i32 %680, 3
  %685 = add i32 %666, 96
  %686 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %685, i32 undef, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %687 = extractvalue %dx.types.ResRet.i32 %686, 0
  %688 = extractvalue %dx.types.ResRet.i32 %686, 1
  %689 = extractvalue %dx.types.ResRet.i32 %686, 2
  %690 = extractvalue %dx.types.ResRet.i32 %686, 3
  %691 = add i32 %666, 268
  %692 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %268, i32 %691, i32 undef, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %693 = extractvalue %dx.types.ResRet.i32 %692, 0
  br label %694

; <label>:694                                     ; preds = %662, %581
  %695 = phi i32 [ %669, %662 ], [ %584, %581 ]
  %696 = phi i32 [ %670, %662 ], [ %585, %581 ]
  %697 = phi i32 [ %671, %662 ], [ %586, %581 ]
  %698 = phi i32 [ %672, %662 ], [ %587, %581 ]
  %699 = phi i32 [ %675, %662 ], [ %590, %581 ]
  %700 = phi i32 [ %676, %662 ], [ %591, %581 ]
  %701 = phi i32 [ %677, %662 ], [ %592, %581 ]
  %702 = phi i32 [ %678, %662 ], [ %593, %581 ]
  %703 = phi i32 [ %681, %662 ], [ %596, %581 ]
  %704 = phi i32 [ %682, %662 ], [ %597, %581 ]
  %705 = phi i32 [ %683, %662 ], [ %598, %581 ]
  %706 = phi i32 [ %684, %662 ], [ %599, %581 ]
  %707 = phi i32 [ %687, %662 ], [ %602, %581 ]
  %708 = phi i32 [ %688, %662 ], [ %603, %581 ]
  %709 = phi i32 [ %689, %662 ], [ %604, %581 ]
  %710 = phi i32 [ %690, %662 ], [ %605, %581 ]
  %711 = phi i32 [ %693, %662 ], [ %622, %581 ]
  %712 = phi i32 [ %664, %662 ], [ %264, %581 ]
  %713 = bitcast i32 %710 to float
  %714 = bitcast i32 %709 to float
  %715 = bitcast i32 %708 to float
  %716 = bitcast i32 %707 to float
  %717 = bitcast i32 %706 to float
  %718 = bitcast i32 %705 to float
  %719 = bitcast i32 %704 to float
  %720 = bitcast i32 %703 to float
  %721 = bitcast i32 %702 to float
  %722 = bitcast i32 %701 to float
  %723 = bitcast i32 %700 to float
  %724 = bitcast i32 %699 to float
  %725 = bitcast i32 %698 to float
  %726 = bitcast i32 %697 to float
  %727 = bitcast i32 %696 to float
  %728 = bitcast i32 %695 to float
  %729 = fmul fast float %728, %640
  %730 = call float @dx.op.tertiary.f32(i32 46, float %641, float %724, float %729)  ; FMad(a,b,c)
  %731 = call float @dx.op.tertiary.f32(i32 46, float %642, float %720, float %730)  ; FMad(a,b,c)
  %732 = fadd fast float %731, %716
  %733 = fmul fast float %727, %640
  %734 = call float @dx.op.tertiary.f32(i32 46, float %641, float %723, float %733)  ; FMad(a,b,c)
  %735 = call float @dx.op.tertiary.f32(i32 46, float %642, float %719, float %734)  ; FMad(a,b,c)
  %736 = fadd fast float %735, %715
  %737 = fmul fast float %726, %640
  %738 = call float @dx.op.tertiary.f32(i32 46, float %641, float %722, float %737)  ; FMad(a,b,c)
  %739 = call float @dx.op.tertiary.f32(i32 46, float %642, float %718, float %738)  ; FMad(a,b,c)
  %740 = fadd fast float %739, %714
  %741 = fmul fast float %725, %640
  %742 = call float @dx.op.tertiary.f32(i32 46, float %641, float %721, float %741)  ; FMad(a,b,c)
  %743 = call float @dx.op.tertiary.f32(i32 46, float %642, float %717, float %742)  ; FMad(a,b,c)
  %744 = fadd fast float %743, %713
  %745 = fdiv fast float %732, %744
  %746 = fdiv fast float %736, %744
  %747 = fdiv fast float %740, %744
  %748 = fmul fast float %745, 1.280000e+02
  %749 = fmul fast float %746, 1.280000e+02
  %750 = fptoui float %748 to i32
  %751 = fptoui float %749 to i32
  %752 = and i32 %711, 31
  %753 = lshr i32 %750, %752
  %754 = lshr i32 %751, %752
  %755 = icmp slt i32 %712, 8192
  br i1 %755, label %756, label %759

; <label>:756                                     ; preds = %694
  %757 = lshr i32 %712, 7
  %758 = and i32 %712, 127
  br label %780

; <label>:759                                     ; preds = %694
  %760 = add i32 %712, -8191
  %761 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %14, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %762 = extractvalue %dx.types.CBufRet.i32 %761, 0
  %763 = and i32 %762, 31
  %764 = lshr i32 %760, %763
  %765 = mul i32 %764, 192
  %766 = extractvalue %dx.types.CBufRet.i32 %761, 1
  %767 = and i32 %766, %760
  %768 = shl i32 %767, 7
  %769 = icmp eq i32 %711, 0
  br i1 %769, label %775, label %770

; <label>:770                                     ; preds = %759
  %771 = sub i32 8, %711
  %772 = and i32 %771, 31
  %773 = shl i32 127, %772
  %774 = and i32 %773, 127
  br label %775

; <label>:775                                     ; preds = %770, %759
  %776 = phi i32 [ %774, %770 ], [ 0, %759 ]
  %777 = phi i32 [ 128, %770 ], [ 0, %759 ]
  %778 = or i32 %776, %768
  %779 = add i32 %777, %765
  br label %780

; <label>:780                                     ; preds = %775, %756
  %781 = phi i32 [ %758, %756 ], [ %778, %775 ]
  %782 = phi i32 [ %757, %756 ], [ %779, %775 ]
  %783 = select i1 %755, i32 0, i32 %753
  %784 = add i32 %781, %783
  %785 = select i1 %755, i32 0, i32 %754
  %786 = add i32 %782, %785
  %787 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 2, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2D<U32>
  %788 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %787, i32 0, i32 %784, i32 %786, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %789 = extractvalue %dx.types.ResRet.i32 %788, 0
  %790 = lshr i32 %789, 20
  %791 = icmp slt i32 %789, 0
  %792 = add i32 %790, %711
  %793 = and i32 %792, 31
  %794 = lshr i32 16384, %793
  %795 = uitofp i32 %794 to float
  %796 = select i1 %755, float 1.280000e+02, float %795
  br i1 %791, label %797, label %814

; <label>:797                                     ; preds = %780
  %798 = lshr i32 %789, 3
  %799 = and i32 %798, 130944
  %800 = fmul fast float %796, %746
  %801 = fptoui float %800 to i32
  %802 = and i32 %801, 127
  %803 = or i32 %802, %799
  %804 = shl i32 %789, 7
  %805 = and i32 %804, 130944
  %806 = fmul fast float %796, %745
  %807 = fptoui float %806 to i32
  %808 = and i32 %807, 127
  %809 = or i32 %808, %805
  %810 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 7, i32 261 })  ; AnnotateHandle(res,props)  resource: Texture2DArray<U32>
  %811 = call %dx.types.ResRet.i32 @dx.op.textureLoad.i32(i32 66, %dx.types.Handle %810, i32 0, i32 %809, i32 %803, i32 0, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %812 = extractvalue %dx.types.ResRet.i32 %811, 0
  %813 = bitcast i32 %812 to float
  br label %814

; <label>:814                                     ; preds = %797, %780
  %815 = phi i1 [ true, %797 ], [ false, %780 ]
  %816 = phi float [ %813, %797 ], [ 0.000000e+00, %780 ]
  %817 = fcmp fast ogt float %816, %747
  %818 = and i1 %815, %817
  %819 = select i1 %818, float 0.000000e+00, float 1.000000e+00
  br label %820

; <label>:820                                     ; preds = %814, %575, %309
  %821 = phi float [ 1.000000e+00, %309 ], [ %580, %575 ], [ %819, %814 ]
  %822 = fmul fast float %821, %262
  %823 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %17, i32 13)  ; CBufferLoadLegacy(handle,regIndex)
  %824 = extractvalue %dx.types.CBufRet.i32 %823, 1
  %825 = icmp eq i32 %824, 0
  br i1 %825, label %895, label %826

; <label>:826                                     ; preds = %820
  %827 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 13)  ; CBufferLoadLegacy(handle,regIndex)
  %828 = extractvalue %dx.types.CBufRet.f32 %827, 0
  %829 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 8)  ; CBufferLoadLegacy(handle,regIndex)
  %830 = extractvalue %dx.types.CBufRet.f32 %829, 1
  %831 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 9)  ; CBufferLoadLegacy(handle,regIndex)
  %832 = extractvalue %dx.types.CBufRet.f32 %831, 0
  %833 = extractvalue %dx.types.CBufRet.f32 %831, 1
  %834 = extractvalue %dx.types.CBufRet.f32 %831, 2
  %835 = extractvalue %dx.types.CBufRet.f32 %831, 3
  %836 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 10)  ; CBufferLoadLegacy(handle,regIndex)
  %837 = extractvalue %dx.types.CBufRet.f32 %836, 0
  %838 = extractvalue %dx.types.CBufRet.f32 %836, 1
  %839 = extractvalue %dx.types.CBufRet.f32 %836, 2
  %840 = extractvalue %dx.types.CBufRet.f32 %836, 3
  %841 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 11)  ; CBufferLoadLegacy(handle,regIndex)
  %842 = extractvalue %dx.types.CBufRet.f32 %841, 0
  %843 = extractvalue %dx.types.CBufRet.f32 %841, 1
  %844 = extractvalue %dx.types.CBufRet.f32 %841, 2
  %845 = extractvalue %dx.types.CBufRet.f32 %841, 3
  %846 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %17, i32 12)  ; CBufferLoadLegacy(handle,regIndex)
  %847 = extractvalue %dx.types.CBufRet.f32 %846, 0
  %848 = extractvalue %dx.types.CBufRet.f32 %846, 1
  %849 = extractvalue %dx.types.CBufRet.f32 %846, 2
  %850 = extractvalue %dx.types.CBufRet.f32 %846, 3
  %851 = fmul fast float %832, %91
  %852 = call float @dx.op.tertiary.f32(i32 46, float %92, float %837, float %851)  ; FMad(a,b,c)
  %853 = call float @dx.op.tertiary.f32(i32 46, float %93, float %842, float %852)  ; FMad(a,b,c)
  %854 = fadd fast float %853, %847
  %855 = fmul fast float %833, %91
  %856 = call float @dx.op.tertiary.f32(i32 46, float %92, float %838, float %855)  ; FMad(a,b,c)
  %857 = call float @dx.op.tertiary.f32(i32 46, float %93, float %843, float %856)  ; FMad(a,b,c)
  %858 = fadd fast float %857, %848
  %859 = fmul fast float %834, %91
  %860 = call float @dx.op.tertiary.f32(i32 46, float %92, float %839, float %859)  ; FMad(a,b,c)
  %861 = call float @dx.op.tertiary.f32(i32 46, float %93, float %844, float %860)  ; FMad(a,b,c)
  %862 = fadd fast float %861, %849
  %863 = fmul fast float %835, %91
  %864 = call float @dx.op.tertiary.f32(i32 46, float %92, float %840, float %863)  ; FMad(a,b,c)
  %865 = call float @dx.op.tertiary.f32(i32 46, float %93, float %845, float %864)  ; FMad(a,b,c)
  %866 = fadd fast float %865, %850
  %867 = fdiv fast float %854, %866
  %868 = fdiv fast float %858, %866
  %869 = fdiv fast float %862, %866
  %870 = fmul fast float %867, 5.000000e-01
  %871 = fmul fast float %868, 5.000000e-01
  %872 = fadd fast float %870, 5.000000e-01
  %873 = fsub fast float 5.000000e-01, %871
  %874 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 777 })  ; AnnotateHandle(res,props)  resource: Texture2D<3xF32>
  %875 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %7, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %876 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %874, %dx.types.Handle %875, float %872, float %873, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %877 = extractvalue %dx.types.ResRet.f32 %876, 0
  %878 = extractvalue %dx.types.ResRet.f32 %876, 1
  %879 = extractvalue %dx.types.ResRet.f32 %876, 2
  %880 = fsub fast float 1.000000e+00, %869
  %881 = call float @dx.op.unary.f32(i32 7, float %880)  ; Saturate(value)
  %882 = fmul fast float %881, %830
  %883 = fsub fast float %882, %877
  %884 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %883)  ; FMax(a,b)
  %885 = fmul fast float %878, 1.000000e+03
  %886 = fmul fast float %885, %884
  %887 = call float @dx.op.binary.f32(i32 36, float %879, float %886)  ; FMin(a,b)
  %888 = fmul fast float %887, 0xBFF7154760000000
  %889 = call float @dx.op.unary.f32(i32 21, float %888)  ; Exp(value)
  %890 = call float @dx.op.unary.f32(i32 7, float %889)  ; Saturate(value)
  %891 = fadd fast float %890, -1.000000e+00
  %892 = fmul fast float %891, %828
  %893 = fadd fast float %892, 1.000000e+00
  %894 = fmul fast float %893, %822
  br label %895

; <label>:895                                     ; preds = %826, %820
  %896 = phi float [ %894, %826 ], [ %822, %820 ]
  %897 = icmp eq i32 %81, 1
  br i1 %897, label %898, label %929

; <label>:898                                     ; preds = %895
  %899 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 171)  ; CBufferLoadLegacy(handle,regIndex)
  %900 = extractvalue %dx.types.CBufRet.f32 %899, 3
  %901 = fmul fast float %900, %27
  %902 = fmul fast float %900, 4.000000e+00
  %903 = fadd fast float %902, 1.000000e+00
  %904 = fmul fast float %903, %19
  %905 = fmul fast float %903, %20
  %906 = fmul fast float %901, %903
  %907 = fmul fast float %900, 2.000000e+00
  %908 = fsub fast float -5.000000e-01, %907
  %909 = fadd fast float %908, %904
  %910 = fadd fast float %908, %905
  %911 = fadd fast float %908, %906
  %912 = call float @dx.op.unary.f32(i32 6, float %909)  ; FAbs(value)
  %913 = call float @dx.op.unary.f32(i32 6, float %910)  ; FAbs(value)
  %914 = call float @dx.op.unary.f32(i32 6, float %911)  ; FAbs(value)
  %915 = fsub fast float 5.000000e-01, %912
  %916 = fsub fast float 5.000000e-01, %913
  %917 = fsub fast float 5.000000e-01, %914
  %918 = fmul fast float %915, 1.000000e+01
  %919 = fmul fast float %916, 1.000000e+01
  %920 = fmul fast float %917, 1.000000e+01
  %921 = call float @dx.op.unary.f32(i32 7, float %918)  ; Saturate(value)
  %922 = call float @dx.op.unary.f32(i32 7, float %919)  ; Saturate(value)
  %923 = call float @dx.op.unary.f32(i32 7, float %920)  ; Saturate(value)
  %924 = fadd fast float %896, -1.000000e+00
  %925 = fmul fast float %921, %924
  %926 = fmul fast float %925, %922
  %927 = fmul fast float %926, %923
  %928 = fadd fast float %927, 1.000000e+00
  br label %929

; <label>:929                                     ; preds = %898, %895
  %930 = phi float [ %928, %898 ], [ %896, %895 ]
  %931 = fmul fast float %44, 0x3FD45F3060000000
  %932 = fmul fast float %45, 0x3FD45F3060000000
  %933 = fmul fast float %46, 0x3FD45F3060000000
  %934 = fmul fast float %931, %930
  %935 = fmul fast float %932, %930
  %936 = fmul fast float %933, %930
  %937 = extractvalue %dx.types.CBufRet.i32 %823, 2
  %938 = icmp eq i32 %937, 0
  br i1 %938, label %1024, label %939

; <label>:939                                     ; preds = %929
  %940 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 194)  ; CBufferLoadLegacy(handle,regIndex)
  %941 = extractvalue %dx.types.CBufRet.f32 %940, 0
  %942 = extractvalue %dx.types.CBufRet.f32 %940, 1
  %943 = extractvalue %dx.types.CBufRet.f32 %940, 2
  %944 = fsub fast float %91, %941
  %945 = fsub fast float %92, %942
  %946 = fsub fast float %93, %943
  %947 = fmul fast float %944, 0x3EE4F8B580000000
  %948 = fmul fast float %945, 0x3EE4F8B580000000
  %949 = fmul fast float %946, 0x3EE4F8B580000000
  %950 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 200)  ; CBufferLoadLegacy(handle,regIndex)
  %951 = extractvalue %dx.types.CBufRet.f32 %950, 3
  %952 = extractvalue %dx.types.CBufRet.f32 %950, 2
  %953 = call float @dx.op.dot3.f32(i32 55, float %947, float %948, float %949, float %947, float %948, float %949)  ; Dot3(ax,ay,az,bx,by,bz)
  %954 = call float @dx.op.dot3.f32(i32 55, float %77, float %78, float %79, float %77, float %78, float %79)  ; Dot3(ax,ay,az,bx,by,bz)
  %955 = call float @dx.op.dot3.f32(i32 55, float %77, float %78, float %79, float %947, float %948, float %949)  ; Dot3(ax,ay,az,bx,by,bz)
  %956 = fmul fast float %955, 2.000000e+00
  %957 = fmul fast float %952, %952
  %958 = fsub fast float %953, %957
  %959 = fmul fast float %956, %956
  %960 = fmul fast float %954, 4.000000e+00
  %961 = fmul fast float %960, %958
  %962 = fsub fast float %959, %961
  %963 = fcmp fast ult float %962, 0.000000e+00
  br i1 %963, label %974, label %964, !dx.controlflow.hints !41

; <label>:964                                     ; preds = %939
  %965 = call float @dx.op.unary.f32(i32 24, float %962)  ; Sqrt(value)
  %966 = fsub fast float -0.000000e+00, %956
  %967 = fsub fast float %966, %965
  %968 = fsub fast float %965, %956
  %969 = fmul fast float %954, 2.000000e+00
  %970 = fdiv fast float %967, %969
  %971 = fdiv fast float %968, %969
  %972 = fcmp ogt float %970, 0.000000e+00
  %973 = fcmp ogt float %971, 0.000000e+00
  br label %974

; <label>:974                                     ; preds = %964, %939
  %975 = phi i1 [ %972, %964 ], [ false, %939 ]
  %976 = phi i1 [ %973, %964 ], [ false, %939 ]
  %977 = or i1 %975, %976
  br i1 %977, label %1017, label %978

; <label>:978                                     ; preds = %974
  %979 = fmul fast float %947, %947
  %980 = fmul fast float %948, %948
  %981 = fadd fast float %979, %980
  %982 = fmul fast float %949, %949
  %983 = fadd fast float %981, %982
  %984 = call float @dx.op.unary.f32(i32 24, float %983)  ; Sqrt(value)
  %985 = fdiv fast float %947, %984
  %986 = fdiv fast float %948, %984
  %987 = fdiv fast float %949, %984
  %988 = call float @dx.op.dot3.f32(i32 55, float %77, float %78, float %79, float %985, float %986, float %987)  ; Dot3(ax,ay,az,bx,by,bz)
  %989 = fmul fast float %951, %951
  %990 = fsub fast float %989, %957
  %991 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %990)  ; FMax(a,b)
  %992 = call float @dx.op.unary.f32(i32 24, float %991)  ; Sqrt(value)
  %993 = fmul fast float %984, %984
  %994 = fsub fast float %993, %957
  %995 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %994)  ; FMax(a,b)
  %996 = call float @dx.op.unary.f32(i32 24, float %995)  ; Sqrt(value)
  %997 = fmul fast float %988, %988
  %998 = fadd fast float %997, -1.000000e+00
  %999 = fmul fast float %993, %998
  %1000 = fadd fast float %999, %989
  %1001 = fmul fast float %988, %984
  %1002 = call float @dx.op.unary.f32(i32 24, float %1000)  ; Sqrt(value)
  %1003 = fsub fast float %1002, %1001
  %1004 = call float @dx.op.binary.f32(i32 35, float 0.000000e+00, float %1003)  ; FMax(a,b)
  %1005 = fsub fast float %951, %984
  %1006 = fsub fast float %1004, %1005
  %1007 = fsub fast float %992, %1005
  %1008 = fadd fast float %1007, %996
  %1009 = fdiv fast float %1006, %1008
  %1010 = fdiv fast float %996, %992
  %1011 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %1012 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %9, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %1013 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %1011, %dx.types.Handle %1012, float %1009, float %1010, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %1014 = extractvalue %dx.types.ResRet.f32 %1013, 0
  %1015 = extractvalue %dx.types.ResRet.f32 %1013, 1
  %1016 = extractvalue %dx.types.ResRet.f32 %1013, 2
  br label %1017

; <label>:1017                                    ; preds = %978, %974
  %1018 = phi float [ %1014, %978 ], [ 0.000000e+00, %974 ]
  %1019 = phi float [ %1015, %978 ], [ 0.000000e+00, %974 ]
  %1020 = phi float [ %1016, %978 ], [ 0.000000e+00, %974 ]
  %1021 = fmul fast float %1018, %934
  %1022 = fmul fast float %1019, %935
  %1023 = fmul fast float %1020, %936
  br label %1024

; <label>:1024                                    ; preds = %1017, %929
  %1025 = phi float [ %1021, %1017 ], [ %934, %929 ]
  %1026 = phi float [ %1022, %1017 ], [ %935, %929 ]
  %1027 = phi float [ %1023, %1017 ], [ %936, %929 ]
  %1028 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 542)  ; CBufferLoadLegacy(handle,regIndex)
  %1029 = extractvalue %dx.types.CBufRet.f32 %1028, 0
  %1030 = extractvalue %dx.types.CBufRet.f32 %1028, 1
  %1031 = extractvalue %dx.types.CBufRet.f32 %1028, 2
  %1032 = extractvalue %dx.types.CBufRet.f32 %1028, 3
  %1033 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 544)  ; CBufferLoadLegacy(handle,regIndex)
  %1034 = extractvalue %dx.types.CBufRet.f32 %1033, 3
  %1035 = extractvalue %dx.types.CBufRet.f32 %1033, 0
  %1036 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 536)  ; CBufferLoadLegacy(handle,regIndex)
  %1037 = extractvalue %dx.types.CBufRet.f32 %1036, 0
  %1038 = extractvalue %dx.types.CBufRet.f32 %1036, 1
  %1039 = extractvalue %dx.types.CBufRet.f32 %1036, 2
  %1040 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 540)  ; CBufferLoadLegacy(handle,regIndex)
  %1041 = extractvalue %dx.types.CBufRet.f32 %1040, 3
  %1042 = extractvalue %dx.types.CBufRet.f32 %1033, 1
  %1043 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 84)  ; CBufferLoadLegacy(handle,regIndex)
  %1044 = extractvalue %dx.types.CBufRet.f32 %1043, 2
  %1045 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 85)  ; CBufferLoadLegacy(handle,regIndex)
  %1046 = extractvalue %dx.types.CBufRet.f32 %1045, 2
  %1047 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 534)  ; CBufferLoadLegacy(handle,regIndex)
  %1048 = extractvalue %dx.types.CBufRet.f32 %1047, 3
  %1049 = fsub fast float %94, %1042
  %1050 = fadd fast float %1049, %1044
  %1051 = fadd fast float %1050, %1046
  %1052 = fadd fast float %1051, %1048
  %1053 = call float @dx.op.binary.f32(i32 35, float %79, float 0x3E45798EE0000000)  ; FMax(a,b)
  %1054 = fsub fast float 1.000000e+00, %1053
  %1055 = fmul fast float %1054, %1041
  %1056 = fadd fast float %1055, %1053
  %1057 = fdiv fast float %1052, %1056
  %1058 = call float @dx.op.binary.f32(i32 35, float %1057, float 0.000000e+00)  ; FMax(a,b)
  %1059 = fmul fast float %1058, 0xBF847AE140000000
  %1060 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 347)  ; CBufferLoadLegacy(handle,regIndex)
  %1061 = extractvalue %dx.types.CBufRet.f32 %1060, 0
  %1062 = fmul fast float %1037, 0x3FF7154760000000
  %1063 = fmul fast float %1062, %1059
  %1064 = fmul fast float %1063, %1061
  %1065 = fmul fast float %1038, 0x3FF7154760000000
  %1066 = fmul fast float %1065, %1059
  %1067 = fmul fast float %1066, %1061
  %1068 = fmul fast float %1039, 0x3FF7154760000000
  %1069 = fmul fast float %1068, %1059
  %1070 = fmul fast float %1069, %1061
  %1071 = call float @dx.op.unary.f32(i32 21, float %1064)  ; Exp(value)
  %1072 = call float @dx.op.unary.f32(i32 21, float %1067)  ; Exp(value)
  %1073 = call float @dx.op.unary.f32(i32 21, float %1070)  ; Exp(value)
  %1074 = fmul fast float %91, %91
  %1075 = fmul fast float %92, %92
  %1076 = fadd fast float %1074, %1075
  %1077 = fmul fast float %93, %93
  %1078 = fadd fast float %1076, %1077
  %1079 = call float @dx.op.unary.f32(i32 24, float %1078)  ; Sqrt(value)
  %1080 = fmul fast float %1079, %1035
  %1081 = call float @dx.op.unary.f32(i32 7, float %1080)  ; Saturate(value)
  %1082 = fsub fast float %1034, %1032
  %1083 = fmul fast float %1081, %1082
  %1084 = fadd fast float %1083, %1032
  %1085 = fsub fast float %1029, %1071
  %1086 = fsub fast float %1030, %1072
  %1087 = fsub fast float %1031, %1073
  %1088 = fmul fast float %1084, %1085
  %1089 = fmul fast float %1084, %1086
  %1090 = fmul fast float %1084, %1087
  %1091 = fadd fast float %1088, %1071
  %1092 = fadd fast float %1089, %1072
  %1093 = fadd fast float %1090, %1073
  %1094 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 541)  ; CBufferLoadLegacy(handle,regIndex)
  %1095 = extractvalue %dx.types.CBufRet.f32 %1094, 0
  %1096 = extractvalue %dx.types.CBufRet.f32 %1094, 1
  %1097 = extractvalue %dx.types.CBufRet.f32 %1094, 2
  %1098 = extractvalue %dx.types.CBufRet.f32 %1094, 3
  %1099 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 543)  ; CBufferLoadLegacy(handle,regIndex)
  %1100 = extractvalue %dx.types.CBufRet.f32 %1099, 3
  %1101 = extractvalue %dx.types.CBufRet.f32 %1099, 0
  %1102 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 535)  ; CBufferLoadLegacy(handle,regIndex)
  %1103 = extractvalue %dx.types.CBufRet.f32 %1102, 0
  %1104 = extractvalue %dx.types.CBufRet.f32 %1102, 1
  %1105 = extractvalue %dx.types.CBufRet.f32 %1102, 2
  %1106 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 539)  ; CBufferLoadLegacy(handle,regIndex)
  %1107 = extractvalue %dx.types.CBufRet.f32 %1106, 3
  %1108 = extractvalue %dx.types.CBufRet.f32 %1099, 1
  %1109 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 533)  ; CBufferLoadLegacy(handle,regIndex)
  %1110 = extractvalue %dx.types.CBufRet.f32 %1109, 3
  %1111 = fsub fast float %94, %1108
  %1112 = fadd fast float %1111, %1044
  %1113 = fadd fast float %1112, %1046
  %1114 = fadd fast float %1113, %1110
  %1115 = fmul fast float %1054, %1107
  %1116 = fadd fast float %1115, %1053
  %1117 = fdiv fast float %1114, %1116
  %1118 = call float @dx.op.binary.f32(i32 35, float %1117, float 0.000000e+00)  ; FMax(a,b)
  %1119 = fmul fast float %1118, 0xBF847AE140000000
  %1120 = fmul fast float %1103, 0x3FF7154760000000
  %1121 = fmul fast float %1120, %1119
  %1122 = fmul fast float %1121, %1061
  %1123 = fmul fast float %1104, 0x3FF7154760000000
  %1124 = fmul fast float %1123, %1119
  %1125 = fmul fast float %1124, %1061
  %1126 = fmul fast float %1105, 0x3FF7154760000000
  %1127 = fmul fast float %1126, %1119
  %1128 = fmul fast float %1127, %1061
  %1129 = call float @dx.op.unary.f32(i32 21, float %1122)  ; Exp(value)
  %1130 = call float @dx.op.unary.f32(i32 21, float %1125)  ; Exp(value)
  %1131 = call float @dx.op.unary.f32(i32 21, float %1128)  ; Exp(value)
  %1132 = fmul fast float %1079, %1101
  %1133 = call float @dx.op.unary.f32(i32 7, float %1132)  ; Saturate(value)
  %1134 = fsub fast float %1100, %1098
  %1135 = fmul fast float %1133, %1134
  %1136 = fadd fast float %1135, %1098
  %1137 = fsub fast float %1095, %1129
  %1138 = fsub fast float %1096, %1130
  %1139 = fsub fast float %1097, %1131
  %1140 = fmul fast float %1136, %1137
  %1141 = fmul fast float %1136, %1138
  %1142 = fmul fast float %1136, %1139
  %1143 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %16, i32 555)  ; CBufferLoadLegacy(handle,regIndex)
  %1144 = extractvalue %dx.types.CBufRet.f32 %1143, 0
  %1145 = fsub fast float %1129, %1091
  %1146 = fadd fast float %1145, %1140
  %1147 = fsub fast float %1130, %1092
  %1148 = fadd fast float %1147, %1141
  %1149 = fsub fast float %1131, %1093
  %1150 = fadd fast float %1149, %1142
  %1151 = fmul fast float %1146, %1144
  %1152 = fmul fast float %1148, %1144
  %1153 = fmul fast float %1150, %1144
  %1154 = fadd fast float %1151, %1091
  %1155 = fadd fast float %1152, %1092
  %1156 = fadd fast float %1153, %1093
  %1157 = fmul fast float %1154, %1025
  %1158 = fmul fast float %1155, %1026
  %1159 = fmul fast float %1156, %1027
  %1160 = fmul fast float %78, 0xBFDF454580000000
  %1161 = fmul fast float %79, 0x3FDF454580000000
  %1162 = fmul fast float %77, 0xBFDF454580000000
  %1163 = fmul fast float %1157, %1160
  %1164 = fmul fast float %1157, %1161
  %1165 = fmul fast float %1157, %1162
  %1166 = fmul fast float %1158, %1160
  %1167 = fmul fast float %1158, %1161
  %1168 = fmul fast float %1158, %1162
  %1169 = fmul fast float %1159, %1160
  %1170 = fmul fast float %1159, %1161
  %1171 = fmul fast float %1159, %1162
  %1172 = call float @dx.op.dot3.f32(i32 55, float %1163, float %1166, float %1169, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1173 = call float @dx.op.dot3.f32(i32 55, float %1164, float %1167, float %1170, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1174 = call float @dx.op.dot3.f32(i32 55, float %1165, float %1168, float %1171, float 0x3FCB37C140000000, float 0x3FE6E2A960000000, float 0x3FB27B3220000000)  ; Dot3(ax,ay,az,bx,by,bz)
  %1175 = fmul fast float %74, 0x3FD20DD820000000
  %1176 = fmul fast float %1175, %1157
  %1177 = fmul fast float %1175, %1158
  %1178 = fmul fast float %1175, %1159
  %1179 = fmul fast float %930, %74
  %1180 = fmul fast float %1172, %74
  %1181 = fmul fast float %1173, %74
  %1182 = fmul fast float %1174, %74
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %1176)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %1177)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %1178)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %1179)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %1180)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %1181)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %1182)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
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
declare %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32) #2

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
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

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
!dx.viewIdState = !{!23}
!dx.entryPoints = !{!24}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !14, !19}
!5 = !{!6, !8, !9, !11, !12, !13}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{i32 1, %struct.ByteAddressBuffer* undef, !"", i32 0, i32 1, i32 1, i32 11, i32 0, null}
!9 = !{i32 2, %"class.Texture2D<unsigned int>"* undef, !"", i32 0, i32 2, i32 1, i32 2, i32 0, !10}
!10 = !{i32 0, i32 5}
!11 = !{i32 3, %"class.Texture2DArray<unsigned int>"* undef, !"", i32 0, i32 3, i32 1, i32 7, i32 0, !10}
!12 = !{i32 4, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 4, i32 1, i32 2, i32 0, !7}
!13 = !{i32 5, %"class.Texture2D<vector<float, 3> >"* undef, !"", i32 0, i32 5, i32 1, i32 2, i32 0, !7}
!14 = !{!15, !16, !17, !18}
!15 = !{i32 0, %"hostlayout.$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 260, null}
!16 = !{i32 1, %hostlayout.View* undef, !"", i32 0, i32 1, i32 1, i32 10076, null}
!17 = !{i32 2, %DeferredLightUniforms* undef, !"", i32 0, i32 2, i32 1, i32 184, null}
!18 = !{i32 3, %VirtualShadowMap* undef, !"", i32 0, i32 3, i32 1, i32 308, null}
!19 = !{!20, !21, !22}
!20 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!21 = !{i32 1, %struct.SamplerState* undef, !"", i32 0, i32 1, i32 1, i32 0, null}
!22 = !{i32 2, %struct.SamplerState* undef, !"", i32 0, i32 2, i32 1, i32 0, null}
!23 = !{[11 x i32] [i32 9, i32 8, i32 127, i32 127, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 127]}
!24 = !{void ()* @InjectMainPS, !"InjectMainPS", !25, !4, !38}
!25 = !{!26, !33, null}
!26 = !{!27, !30, !31}
!27 = !{i32 0, !"TEXCOORD", i8 9, i8 0, !28, i8 4, i32 1, i8 2, i32 0, i8 0, !29}
!28 = !{i32 0}
!29 = !{i32 3, i32 3}
!30 = !{i32 1, !"SV_Position", i8 9, i8 3, !28, i8 4, i32 1, i8 4, i32 1, i8 0, null}
!31 = !{i32 2, !"SV_RenderTargetArrayIndex", i8 5, i8 4, !28, i8 1, i32 1, i8 1, i32 2, i8 0, !32}
!32 = !{i32 3, i32 1}
!33 = !{!34, !36}
!34 = !{i32 0, !"SV_Target", i8 9, i8 16, !28, i8 0, i32 1, i8 4, i32 0, i8 0, !35}
!35 = !{i32 3, i32 15}
!36 = !{i32 1, !"SV_Target", i8 9, i8 16, !37, i8 0, i32 1, i8 4, i32 1, i8 0, !35}
!37 = !{i32 1}
!38 = !{i32 0, i64 16, i32 5, !28}
!39 = distinct !{!39, !"dx.controlflow.hints", i32 1}
!40 = distinct !{!40, !"dx.controlflow.hints", i32 1}
!41 = distinct !{!41, !"dx.controlflow.hints", i32 2}
