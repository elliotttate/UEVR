;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Position              0   xyzw        0      POS   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader debug name: 3189dbd0525f22a4e9def5e4762c50cb.pdb
; shader hash: 3189dbd0525f22a4e9def5e4762c50cb
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
; SigInputElements: 1
; SigOutputElements: 1
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 1
; SigOutputVectors[0]: 1
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: MainPS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Position              0          noperspective       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
;
; Buffer Definitions:
;
; cbuffer 
; {
;
;   [16 x i8] (type annotation not present)
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
;   [44 x i8] (type annotation not present)
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
;                                   sampler      NA          NA      S0             s0     1
;                                   texture     f32          2d      T0             t0     1
;
;
; ViewId state:
;
; Number of inputs: 4, outputs: 4
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1 }
;   output 1 depends on inputs: { 0, 1 }
;   output 2 depends on inputs: { 0, 1 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"$Globals" = type { <2 x i32>, <2 x float> }
%hostlayout.View = type { [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <4 x float>, <4 x float>, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], [4 x <4 x float>], <4 x float>, <4 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <4 x float>, <4 x float>, <4 x i32>, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, float, float, float, float, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, float, float, <4 x float>, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, float, float, float, float, float, float, float, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <3 x float>, float, <4 x float>, [4 x <4 x float>], <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, float, float, float, float, <3 x float>, float, float, float, float, float, <4 x float>, float, float, float, float, <4 x float>, float, float, float, float, [8 x <4 x float>], float, float, float, float, i32, float, float, float, <3 x float>, i32, [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], [6 x <4 x float>], float, float, i32, i32, <3 x float>, float, <3 x float>, float, float, float, i32, float, float, float, float, float, float, float, <2 x i32>, float, float, float, float, <3 x float>, float, <3 x float>, float, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, float, float, <3 x float>, float, <2 x float>, <2 x float>, float, float, float, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, float, float, float, float, [2 x <4 x float>], <4 x float>, i32, i32, i32, i32, i32, i32, i32, float, <4 x float>, i32, i32, i32, i32, <4 x i32>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, <3 x float>, float, i32, i32, i32, i32, [32 x <4 x i32>], i32, float, float, float, <4 x float>, <4 x float>, <4 x float>, <4 x float>, <2 x float>, float, float, <4 x float>, <4 x float>, <4 x float>, float, float, float, i32, <4 x i32>, i32, i32, i32, i32, [4 x <4 x float>], <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x i32>, i32, [16 x <4 x float>], float, float, float, float, float, float, float, float, [4 x [4 x <4 x float>]], [4 x <4 x float>], [4 x <4 x float>], [16 x [4 x <4 x float>]], [16 x <4 x float>], [16 x <4 x float>], [8 x [4 x <4 x float>]], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [8 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], [2 x <4 x float>], <4 x float>, <4 x float>, float, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x float>, i32, <4 x float>, <4 x float>, <4 x float>, <3 x float>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float>, float, <3 x float>, float, <3 x float>, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <4 x float>, float, float, i32, i32, i32, i32, i32, i32, <4 x float>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, <3 x i32>, i32, <3 x i32>, i32, <3 x float>, float, <3 x float> }
%Material = type { [2 x <4 x float>], i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @MainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 2 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 13, i32 44 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %7 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 13, i32 10076 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 13, i32 16 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %9 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !25  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %10 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !25  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %11 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 18)  ; CBufferLoadLegacy(handle,regIndex)
  %12 = extractvalue %dx.types.CBufRet.f32 %11, 0
  %13 = extractvalue %dx.types.CBufRet.f32 %11, 1
  %14 = extractvalue %dx.types.CBufRet.f32 %11, 2
  %15 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 31)  ; CBufferLoadLegacy(handle,regIndex)
  %16 = extractvalue %dx.types.CBufRet.f32 %15, 3
  %17 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 73)  ; CBufferLoadLegacy(handle,regIndex)
  %18 = extractvalue %dx.types.CBufRet.f32 %17, 0
  %19 = extractvalue %dx.types.CBufRet.f32 %17, 1
  %20 = extractvalue %dx.types.CBufRet.f32 %17, 2
  %21 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 121)  ; CBufferLoadLegacy(handle,regIndex)
  %22 = extractvalue %dx.types.CBufRet.f32 %21, 2
  %23 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 122)  ; CBufferLoadLegacy(handle,regIndex)
  %24 = extractvalue %dx.types.CBufRet.f32 %23, 2
  %25 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.i32 %25, 0
  %27 = extractvalue %dx.types.CBufRet.i32 %25, 1
  %28 = uitofp i32 %26 to float
  %29 = uitofp i32 %27 to float
  %30 = fsub float %9, %28
  %31 = fsub float %10, %29
  %32 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %33 = extractvalue %dx.types.CBufRet.f32 %32, 2
  %34 = extractvalue %dx.types.CBufRet.f32 %32, 3
  %35 = fmul float %33, %30
  %36 = fmul float %34, %31
  %37 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 149)  ; CBufferLoadLegacy(handle,regIndex)
  %38 = extractvalue %dx.types.CBufRet.f32 %37, 0
  %39 = extractvalue %dx.types.CBufRet.f32 %37, 1
  %40 = fmul float %38, %35
  %41 = fmul float %39, %36
  %42 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 148)  ; CBufferLoadLegacy(handle,regIndex)
  %43 = extractvalue %dx.types.CBufRet.f32 %42, 0
  %44 = extractvalue %dx.types.CBufRet.f32 %42, 1
  %45 = fadd float %43, %40
  %46 = fadd float %44, %41
  %47 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 152)  ; CBufferLoadLegacy(handle,regIndex)
  %48 = extractvalue %dx.types.CBufRet.f32 %47, 2
  %49 = extractvalue %dx.types.CBufRet.f32 %47, 3
  %50 = fmul float %48, %45
  %51 = fmul float %49, %46
  %52 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %53 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %54 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %52, %dx.types.Handle %53, float %50, float %51, float undef, float undef, i32 0, i32 0, i32 undef, float 0.000000e+00)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %55 = extractvalue %dx.types.ResRet.f32 %54, 0
  %56 = call float @dx.op.binary.f32(i32 35, float %55, float 0x3C32725DE0000000), !dx.precise !25  ; FMax(a,b)
  %57 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 44)  ; CBufferLoadLegacy(handle,regIndex)
  %58 = extractvalue %dx.types.CBufRet.f32 %57, 0
  %59 = extractvalue %dx.types.CBufRet.f32 %57, 1
  %60 = extractvalue %dx.types.CBufRet.f32 %57, 2
  %61 = extractvalue %dx.types.CBufRet.f32 %57, 3
  %62 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 45)  ; CBufferLoadLegacy(handle,regIndex)
  %63 = extractvalue %dx.types.CBufRet.f32 %62, 0
  %64 = extractvalue %dx.types.CBufRet.f32 %62, 1
  %65 = extractvalue %dx.types.CBufRet.f32 %62, 2
  %66 = extractvalue %dx.types.CBufRet.f32 %62, 3
  %67 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 46)  ; CBufferLoadLegacy(handle,regIndex)
  %68 = extractvalue %dx.types.CBufRet.f32 %67, 0
  %69 = extractvalue %dx.types.CBufRet.f32 %67, 1
  %70 = extractvalue %dx.types.CBufRet.f32 %67, 2
  %71 = extractvalue %dx.types.CBufRet.f32 %67, 3
  %72 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %7, i32 47)  ; CBufferLoadLegacy(handle,regIndex)
  %73 = extractvalue %dx.types.CBufRet.f32 %72, 0
  %74 = extractvalue %dx.types.CBufRet.f32 %72, 1
  %75 = extractvalue %dx.types.CBufRet.f32 %72, 2
  %76 = extractvalue %dx.types.CBufRet.f32 %72, 3
  %77 = fmul float %9, %58
  %78 = call float @dx.op.tertiary.f32(i32 46, float %10, float %63, float %77), !dx.precise !25  ; FMad(a,b,c)
  %79 = call float @dx.op.tertiary.f32(i32 46, float %56, float %68, float %78), !dx.precise !25  ; FMad(a,b,c)
  %80 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %73, float %79), !dx.precise !25  ; FMad(a,b,c)
  %81 = fmul float %9, %59
  %82 = call float @dx.op.tertiary.f32(i32 46, float %10, float %64, float %81), !dx.precise !25  ; FMad(a,b,c)
  %83 = call float @dx.op.tertiary.f32(i32 46, float %56, float %69, float %82), !dx.precise !25  ; FMad(a,b,c)
  %84 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %74, float %83), !dx.precise !25  ; FMad(a,b,c)
  %85 = fmul float %9, %60
  %86 = call float @dx.op.tertiary.f32(i32 46, float %10, float %65, float %85), !dx.precise !25  ; FMad(a,b,c)
  %87 = call float @dx.op.tertiary.f32(i32 46, float %56, float %70, float %86), !dx.precise !25  ; FMad(a,b,c)
  %88 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %75, float %87), !dx.precise !25  ; FMad(a,b,c)
  %89 = fmul float %9, %61
  %90 = call float @dx.op.tertiary.f32(i32 46, float %10, float %66, float %89), !dx.precise !25  ; FMad(a,b,c)
  %91 = call float @dx.op.tertiary.f32(i32 46, float %56, float %71, float %90), !dx.precise !25  ; FMad(a,b,c)
  %92 = call float @dx.op.tertiary.f32(i32 46, float 1.000000e+00, float %76, float %91), !dx.precise !25  ; FMad(a,b,c)
  %93 = fdiv float %80, %92
  %94 = fdiv float %84, %92
  %95 = fdiv float %88, %92
  %96 = fsub fast float -0.000000e+00, %93
  %97 = fsub fast float -0.000000e+00, %94
  %98 = fsub fast float -0.000000e+00, %95
  %99 = call float @dx.op.dot3.f32(i32 55, float %96, float %97, float %98, float %96, float %97, float %98)  ; Dot3(ax,ay,az,bx,by,bz)
  %100 = call float @dx.op.unary.f32(i32 25, float %99)  ; Rsqrt(value)
  %101 = fmul fast float %100, %96
  %102 = fmul fast float %100, %97
  %103 = fmul fast float %100, %98
  %104 = fsub fast float -0.000000e+00, %18
  %105 = fsub fast float -0.000000e+00, %19
  %106 = fsub fast float -0.000000e+00, %20
  %107 = fcmp fast oge float %16, 1.000000e+00
  %108 = select i1 %107, float %104, float %101
  %109 = select i1 %107, float %105, float %102
  %110 = select i1 %107, float %106, float %103
  %111 = call float @dx.op.dot3.f32(i32 55, float %108, float %109, float %110, float %12, float %13, float %14)  ; Dot3(ax,ay,az,bx,by,bz)
  %112 = call float @dx.op.unary.f32(i32 6, float %111)  ; FAbs(value)
  %113 = fdiv fast float %110, %112
  %114 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %115 = extractvalue %dx.types.CBufRet.f32 %114, 0
  %116 = fmul fast float %115, %113
  %117 = fsub fast float %24, %116
  %118 = extractvalue %dx.types.CBufRet.f32 %114, 1
  %119 = fmul fast float %117, %118
  %120 = extractvalue %dx.types.CBufRet.f32 %114, 2
  %121 = fsub fast float %120, %119
  %122 = fmul fast float %22, 2.097152e+06
  %123 = fmul fast float %122, %118
  %124 = fsub fast float %121, %123
  %125 = call float @dx.op.unary.f32(i32 7, float %124)  ; Saturate(value)
  %126 = extractvalue %dx.types.CBufRet.f32 %114, 3
  %127 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %128 = extractvalue %dx.types.CBufRet.f32 %127, 0
  %129 = extractvalue %dx.types.CBufRet.f32 %127, 1
  %130 = extractvalue %dx.types.CBufRet.f32 %127, 2
  %131 = fsub fast float %128, %125
  %132 = fsub fast float %129, %125
  %133 = fsub fast float %130, %125
  %134 = fmul fast float %131, %126
  %135 = fmul fast float %132, %126
  %136 = fmul fast float %133, %126
  %137 = fadd fast float %134, %125
  %138 = fadd fast float %135, %125
  %139 = fadd fast float %136, %125
  %140 = call float @dx.op.binary.f32(i32 35, float %137, float 0.000000e+00)  ; FMax(a,b)
  %141 = call float @dx.op.binary.f32(i32 35, float %138, float 0.000000e+00)  ; FMax(a,b)
  %142 = call float @dx.op.binary.f32(i32 35, float %139, float 0.000000e+00)  ; FMax(a,b)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %140)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %141)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %142)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #2

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
!dx.viewIdState = !{!14}
!dx.entryPoints = !{!15}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !8, !12}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9, !10, !11}
!9 = !{i32 0, %"$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 16, null}
!10 = !{i32 1, %hostlayout.View* undef, !"", i32 0, i32 1, i32 1, i32 10076, null}
!11 = !{i32 2, %Material* undef, !"", i32 0, i32 2, i32 1, i32 44, null}
!12 = !{!13}
!13 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!14 = !{[6 x i32] [i32 4, i32 4, i32 7, i32 7, i32 0, i32 0]}
!15 = !{void ()* @MainPS, !"MainPS", !16, !4, !24}
!16 = !{!17, !21, null}
!17 = !{!18}
!18 = !{i32 0, !"SV_Position", i8 9, i8 3, !19, i8 4, i32 1, i8 4, i32 0, i8 0, !20}
!19 = !{i32 0}
!20 = !{i32 3, i32 3}
!21 = !{!22}
!22 = !{i32 0, !"SV_Target", i8 9, i8 16, !19, i8 0, i32 1, i8 4, i32 0, i8 0, !23}
!23 = !{i32 3, i32 15}
!24 = !{i32 5, !19}
!25 = !{i32 1}
