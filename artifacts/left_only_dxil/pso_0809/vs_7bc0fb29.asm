;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; ATTRIBUTE                0   xy          0     NONE   float   xy  
; ATTRIBUTE                1   xy          1     NONE   float   xy  
; SV_InstanceID            0   x           2   INSTID    uint   x   
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD                 0   xy          0     NONE   float   xy  
; SV_Position              0   xyzw        1      POS   float   xyzw
; TEXCOORD                 1   x           2     NONE    uint   x   
;
; shader debug name: 0d03f9685023d36f38e1ddef06761734.pdb
; shader hash: 0d03f9685023d36f38e1ddef06761734
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Vertex Shader
; OutputPositionPresent=1
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 3
; SigOutputElements: 3
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 3
; SigOutputVectors[0]: 3
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: WriteToSliceMainVS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; ATTRIBUTE                0                              
; ATTRIBUTE                1                              
; SV_InstanceID            0                              
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD                 0          noperspective       
; SV_Position              0          noperspective       
; TEXCOORD                 1        nointerpolation       
;
; Buffer Definitions:
;
; cbuffer 
; {
;
;   [32 x i8] (type annotation not present)
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;                                   cbuffer      NA          NA     CB0            cb0     1
;
;
; ViewId state:
;
; Number of inputs: 9, outputs: 9
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 4 }
;   output 1 depends on inputs: { 5 }
;   output 4 depends on inputs: { 0 }
;   output 5 depends on inputs: { 1 }
;   output 8 depends on inputs: { 8 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"$Globals" = type { i32, <4 x float> }

define void @WriteToSliceMainVS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 13, i32 32 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %3 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %4 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %5 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %6 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %7 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %6)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %7)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float 1.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  %8 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %2, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %9 = extractvalue %dx.types.CBufRet.f32 %8, 0
  %10 = extractvalue %dx.types.CBufRet.f32 %8, 1
  %11 = fmul fast float %9, %4
  %12 = fmul fast float %10, %5
  %13 = extractvalue %dx.types.CBufRet.f32 %8, 2
  %14 = extractvalue %dx.types.CBufRet.f32 %8, 3
  %15 = fadd fast float %11, %13
  %16 = fadd fast float %12, %14
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %15)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %16)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  %17 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %2, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %18 = extractvalue %dx.types.CBufRet.i32 %17, 0
  %19 = add i32 %18, %3
  call void @dx.op.storeOutput.i32(i32 5, i32 2, i32 0, i8 0, i32 %19)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind
declare void @dx.op.storeOutput.i32(i32, i32, i32, i8, i32) #1

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
!dx.viewIdState = !{!7}
!dx.entryPoints = !{!8}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"vs", i32 6, i32 6}
!4 = !{null, null, !5, null}
!5 = !{!6}
!6 = !{i32 0, %"$Globals"* undef, !"", i32 0, i32 0, i32 1, i32 32, null}
!7 = !{[11 x i32] [i32 9, i32 9, i32 16, i32 32, i32 0, i32 0, i32 1, i32 2, i32 0, i32 0, i32 256]}
!8 = !{void ()* @WriteToSliceMainVS, !"WriteToSliceMainVS", !9, !4, !23}
!9 = !{!10, !18, null}
!10 = !{!11, !14, !16}
!11 = !{i32 0, !"ATTRIBUTE", i8 9, i8 0, !12, i8 0, i32 1, i8 2, i32 0, i8 0, !13}
!12 = !{i32 0}
!13 = !{i32 3, i32 3}
!14 = !{i32 1, !"ATTRIBUTE", i8 9, i8 0, !15, i8 0, i32 1, i8 2, i32 1, i8 0, !13}
!15 = !{i32 1}
!16 = !{i32 2, !"SV_InstanceID", i8 5, i8 2, !12, i8 0, i32 1, i8 1, i32 2, i8 0, !17}
!17 = !{i32 3, i32 1}
!18 = !{!19, !20, !22}
!19 = !{i32 0, !"TEXCOORD", i8 9, i8 0, !12, i8 4, i32 1, i8 2, i32 0, i8 0, !13}
!20 = !{i32 1, !"SV_Position", i8 9, i8 3, !12, i8 4, i32 1, i8 4, i32 1, i8 0, !21}
!21 = !{i32 3, i32 15}
!22 = !{i32 2, !"TEXCOORD", i8 5, i8 0, !15, i8 1, i32 1, i8 1, i32 2, i8 0, !17}
!23 = !{i32 5, !12}
