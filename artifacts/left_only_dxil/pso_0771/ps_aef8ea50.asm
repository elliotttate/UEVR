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
; shader debug name: 8815e8461eb3f10d3832d13aa04da83c.pdb
; shader hash: 8815e8461eb3f10d3832d13aa04da83c
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
;   [180 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [28 x i8] (type annotation not present)
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
;   output 3 depends on inputs: { 0, 1 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"$Globals" = type { <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x float>, <2 x i32>, <2 x float>, i32 }
%Material = type { [1 x <4 x float>], i32, i32, i32 }
%struct.SamplerState = type { i32 }

define void @MainPS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 3 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 13, i32 28 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %6 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 13, i32 180 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %7 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef), !dx.precise !24  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %8 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef), !dx.precise !24  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %9 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %6, i32 10)  ; CBufferLoadLegacy(handle,regIndex)
  %10 = extractvalue %dx.types.CBufRet.i32 %9, 0
  %11 = extractvalue %dx.types.CBufRet.i32 %9, 1
  %12 = uitofp i32 %10 to float
  %13 = uitofp i32 %11 to float
  %14 = fsub fast float %7, %12
  %15 = fsub fast float %8, %13
  %16 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 10)  ; CBufferLoadLegacy(handle,regIndex)
  %17 = extractvalue %dx.types.CBufRet.f32 %16, 2
  %18 = extractvalue %dx.types.CBufRet.f32 %16, 3
  %19 = fmul fast float %14, %17
  %20 = fmul fast float %15, %18
  %21 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %22 = extractvalue %dx.types.CBufRet.f32 %21, 2
  %23 = extractvalue %dx.types.CBufRet.f32 %21, 3
  %24 = fmul fast float %19, %22
  %25 = fmul fast float %20, %23
  %26 = extractvalue %dx.types.CBufRet.f32 %21, 0
  %27 = extractvalue %dx.types.CBufRet.f32 %21, 1
  %28 = fadd fast float %24, %26
  %29 = fadd fast float %25, %27
  %30 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %31 = extractvalue %dx.types.CBufRet.f32 %30, 0
  %32 = extractvalue %dx.types.CBufRet.f32 %30, 1
  %33 = extractvalue %dx.types.CBufRet.f32 %30, 2
  %34 = extractvalue %dx.types.CBufRet.f32 %30, 3
  %35 = call float @dx.op.binary.f32(i32 35, float %28, float %31)  ; FMax(a,b)
  %36 = call float @dx.op.binary.f32(i32 35, float %29, float %32)  ; FMax(a,b)
  %37 = call float @dx.op.binary.f32(i32 36, float %35, float %33)  ; FMin(a,b)
  %38 = call float @dx.op.binary.f32(i32 36, float %36, float %34)  ; FMin(a,b)
  %39 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 2, i32 1033 })  ; AnnotateHandle(res,props)  resource: Texture2D<4xF32>
  %40 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 14, i32 0 })  ; AnnotateHandle(res,props)  resource: SamplerState
  %41 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %39, %dx.types.Handle %40, float %37, float %38, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %42 = extractvalue %dx.types.ResRet.f32 %41, 0
  %43 = extractvalue %dx.types.ResRet.f32 %41, 1
  %44 = extractvalue %dx.types.ResRet.f32 %41, 2
  %45 = extractvalue %dx.types.ResRet.f32 %41, 3
  %46 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %6, i32 11)  ; CBufferLoadLegacy(handle,regIndex)
  %47 = extractvalue %dx.types.CBufRet.i32 %46, 0
  %48 = icmp eq i32 %47, 14
  %49 = select i1 %48, float %45, float 0.000000e+00
  %50 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %5, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %51 = extractvalue %dx.types.CBufRet.f32 %50, 0
  %52 = extractvalue %dx.types.CBufRet.f32 %50, 1
  %53 = extractvalue %dx.types.CBufRet.f32 %50, 2
  %54 = extractvalue %dx.types.CBufRet.f32 %50, 3
  %55 = fsub fast float %52, %42
  %56 = fsub fast float %53, %43
  %57 = fsub fast float %54, %44
  %58 = fmul fast float %55, %51
  %59 = fmul fast float %56, %51
  %60 = fmul fast float %57, %51
  %61 = fadd fast float %58, %42
  %62 = fadd fast float %59, %43
  %63 = fadd fast float %60, %44
  %64 = call float @dx.op.binary.f32(i32 35, float %61, float 0.000000e+00)  ; FMax(a,b)
  %65 = call float @dx.op.binary.f32(i32 35, float %62, float 0.000000e+00)  ; FMax(a,b)
  %66 = call float @dx.op.binary.f32(i32 35, float %63, float 0.000000e+00)  ; FMax(a,b)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %64)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %65)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %66)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %49)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

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
!dx.viewIdState = !{!13}
!dx.entryPoints = !{!14}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"ps", i32 6, i32 6}
!4 = !{!5, null, !8, !11}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9, !10}
!9 = !{i32 0, %"$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 180, null}
!10 = !{i32 1, %Material* undef, !"", i32 0, i32 1, i32 1, i32 28, null}
!11 = !{!12}
!12 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!13 = !{[6 x i32] [i32 4, i32 4, i32 15, i32 15, i32 0, i32 0]}
!14 = !{void ()* @MainPS, !"MainPS", !15, !4, !23}
!15 = !{!16, !20, null}
!16 = !{!17}
!17 = !{i32 0, !"SV_Position", i8 9, i8 3, !18, i8 4, i32 1, i8 4, i32 0, i8 0, !19}
!18 = !{i32 0}
!19 = !{i32 3, i32 3}
!20 = !{!21}
!21 = !{i32 0, !"SV_Target", i8 9, i8 16, !18, i8 0, i32 1, i8 4, i32 0, i8 0, !22}
!22 = !{i32 3, i32 15}
!23 = !{i32 5, !18}
!24 = !{i32 1}
