;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; ATTRIBUTE                0   xyzw        0     NONE   float       
; ATTRIBUTE               13   x           1     NONE    uint   x   
; SV_InstanceID            0   x           2   INSTID    uint   x   
; SV_VertexID              0   x           3   VERTID    uint   x   
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD10_centroid      0   xyzw        0     NONE   float   xyzw
; TEXCOORD11_centroid      0   xyzw        1     NONE   float   xyzw
; PRIMITIVE_ID             0   x           2     NONE    uint   x   
; ATTRIBUTE                0   xy          3     NONE   float   xy  
;
; shader debug name: f0699315f17631765286826a11fe9633.pdb
; shader hash: f0699315f17631765286826a11fe9633
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Vertex Shader
; OutputPositionPresent=0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 4
; SigOutputElements: 4
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 4
; SigOutputVectors[0]: 4
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: VoxelizeVS
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; ATTRIBUTE                0                              
; ATTRIBUTE               13                              
; SV_InstanceID            0                              
; SV_VertexID              0                              
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; TEXCOORD10_centroid      0                 linear       
; TEXCOORD11_centroid      0                 linear       
; PRIMITIVE_ID             0        nointerpolation       
; ATTRIBUTE                0                 linear       
;
; Buffer Definitions:
;
; cbuffer 
; {
;
;   [348 x i8] (type annotation not present)
;
; }
;
; cbuffer 
; {
;
;   [60 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [4 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [16 x i8] (type annotation not present)
;
; }
;
; Resource bind info for 
; {
;
;   [16 x i8] (type annotation not present)
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
;                                   texture  struct         r/o      T0             t0     1
;                                   texture  struct         r/o      T1             t1     1
;                                   texture  struct         r/o      T2             t2     1
;                                   texture     f32         buf      T3             t3     1
;
;
; ViewId state:
;
; Number of inputs: 13, outputs: 14
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 4, 8, 12 }
;   output 1 depends on inputs: { 4, 8, 12 }
;   output 2 depends on inputs: { 4, 8, 12 }
;   output 4 depends on inputs: { 4, 8, 12 }
;   output 5 depends on inputs: { 4, 8, 12 }
;   output 6 depends on inputs: { 4, 8, 12 }
;   output 7 depends on inputs: { 4, 8, 12 }
;   output 8 depends on inputs: { 4, 8 }
;   output 12 depends on inputs: { 12 }
;   output 13 depends on inputs: { 12 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.StructuredBuffer<unsigned int>" = type { i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.Buffer<vector<float, 4> >" = type { <4 x float> }
%Scene = type { i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, <4 x i32>, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, i32, i32, float, <2 x float>, i32, i32, i32, i32, i32, float, i32, i32, i32, i32, i32, float, float, float, i32, i32, i32, float, i32, i32, i32 }
%LocalVF = type { <4 x i32>, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }

define void @VoxelizeVS() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 3, i32 3, i32 0, i8 0 }, i32 3, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 2, i32 2, i32 0, i8 0 }, i32 2, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 2 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %6 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 2 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %7 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 13, i32 60 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 13, i32 348 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %9 = call i32 @dx.op.loadInput.i32(i32 4, i32 3, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %10 = call i32 @dx.op.loadInput.i32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %11 = call i32 @dx.op.loadInput.i32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %12 = icmp slt i32 %11, 0
  br i1 %12, label %13, label %21

; <label>:13                                      ; preds = %0
  %14 = and i32 %11, 2147483647
  %15 = mul i32 %14, 44
  %16 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %17 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %16, i32 %15, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %18 = extractvalue %dx.types.ResRet.f32 %17, 1
  %19 = bitcast float %18 to i32
  %20 = add i32 %19, %10
  br label %27

; <label>:21                                      ; preds = %0
  %22 = add i32 %11, %10
  %23 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %4, %dx.types.ResourceProperties { i32 12, i32 4 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=4>
  %24 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %23, i32 %22, i32 0, i8 1, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %25 = extractvalue %dx.types.ResRet.i32 %24, 0
  %26 = and i32 %25, 16777215
  br label %27

; <label>:27                                      ; preds = %21, %13
  %28 = phi i32 [ %20, %13 ], [ %26, %21 ]
  %29 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %8, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %30 = extractvalue %dx.types.CBufRet.i32 %29, 0
  %31 = extractvalue %dx.types.CBufRet.i32 %29, 1
  %32 = extractvalue %dx.types.CBufRet.i32 %29, 2
  %33 = and i32 %30, 31
  %34 = lshr i32 %28, %33
  %35 = and i32 %31, %28
  %36 = mul i32 %32, %34
  %37 = add i32 %36, %35
  %38 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 12, i32 16 })  ; AnnotateHandle(res,props)  resource: StructuredBuffer<stride=16>
  %39 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %38, i32 %37, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %40 = extractvalue %dx.types.ResRet.f32 %39, 0
  %41 = bitcast float %40 to i32
  %42 = and i32 %41, 1048575
  %43 = lshr i32 %41, 20
  %44 = icmp ne i32 %42, 1048575
  %45 = and i32 %43, 1024
  %46 = icmp eq i32 %45, 0
  %47 = and i1 %44, %46
  br i1 %47, label %48, label %155, !dx.controlflow.hints !35

; <label>:48                                      ; preds = %27
  %49 = shl i32 1, %33
  %50 = add i32 %35, %49
  %51 = add i32 %50, %36
  %52 = call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, %dx.types.Handle %38, i32 %51, i32 0, i8 15, i32 4)  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %53 = extractvalue %dx.types.ResRet.f32 %52, 0
  %54 = extractvalue %dx.types.ResRet.f32 %52, 1
  %55 = extractvalue %dx.types.ResRet.f32 %52, 2
  %56 = extractvalue %dx.types.ResRet.f32 %52, 3
  %57 = bitcast float %53 to i32
  %58 = bitcast float %54 to i32
  %59 = bitcast float %55 to i32
  %60 = bitcast float %56 to i32
  %61 = and i32 %57, 65535
  %62 = uitofp i32 %61 to float
  %63 = lshr i32 %57, 16
  %64 = uitofp i32 %63 to float
  %65 = and i32 %58, 32767
  %66 = uitofp i32 %65 to float
  %67 = fadd fast float %62, -3.276800e+04
  %68 = fadd fast float %64, -3.276800e+04
  %69 = fadd fast float %66, -1.638400e+04
  %70 = fmul fast float %69, 0x3F06A0F8E0000000
  %71 = and i32 %58, 32768
  %72 = icmp ne i32 %71, 0
  %73 = fadd fast float %67, %68
  %74 = fmul fast float %73, 0x3F00002000000000
  %75 = fsub fast float %67, %68
  %76 = fmul fast float %75, 0x3F00002000000000
  %77 = call float @dx.op.unary.f32(i32 6, float %74)  ; FAbs(value)
  %78 = call float @dx.op.unary.f32(i32 6, float %76)  ; FAbs(value)
  %79 = call float @dx.op.dot2.f32(i32 54, float 1.000000e+00, float 1.000000e+00, float %77, float %78)  ; Dot2(ax,ay,bx,by)
  %80 = fsub fast float 2.000000e+00, %79
  %81 = call float @dx.op.dot3.f32(i32 55, float %74, float %76, float %80, float %74, float %76, float %80)  ; Dot3(ax,ay,az,bx,by,bz)
  %82 = call float @dx.op.unary.f32(i32 25, float %81)  ; Rsqrt(value)
  %83 = fmul fast float %74, %82
  %84 = fmul fast float %76, %82
  %85 = fmul fast float %82, %80
  %86 = fadd fast float %85, 1.000000e+00
  %87 = fdiv fast float 1.000000e+00, %86
  %88 = fmul fast float %87, %84
  %89 = fmul fast float %88, %83
  %90 = fsub fast float -0.000000e+00, %89
  %91 = fmul fast float %83, %83
  %92 = fmul fast float %91, %87
  %93 = fsub fast float 1.000000e+00, %92
  %94 = fmul fast float %84, %84
  %95 = fmul fast float %94, %87
  %96 = fsub fast float 1.000000e+00, %95
  %97 = fmul fast float %70, %70
  %98 = fsub fast float 1.000000e+00, %97
  %99 = call float @dx.op.unary.f32(i32 24, float %98)  ; Sqrt(value)
  %100 = select i1 %72, float %70, float %99
  %101 = select i1 %72, float %99, float %70
  %102 = fmul fast float %93, %100
  %103 = fmul fast float %100, %90
  %104 = fmul fast float %83, %100
  %105 = fmul fast float %101, %90
  %106 = fmul fast float %96, %101
  %107 = fmul fast float %101, %84
  %108 = fsub fast float -0.000000e+00, %107
  %109 = fadd fast float %102, %105
  %110 = fadd fast float %103, %106
  %111 = fsub fast float %108, %104
  %112 = fmul fast float %111, %84
  %113 = fmul fast float %110, %85
  %114 = fsub fast float %112, %113
  %115 = fmul fast float %109, %85
  %116 = fmul fast float %111, %83
  %117 = fsub fast float %115, %116
  %118 = fmul fast float %110, %83
  %119 = fmul fast float %109, %84
  %120 = fsub fast float %118, %119
  %121 = lshr i32 %60, 16
  %122 = shl i32 %121, 23
  %123 = add i32 %122, -125829120
  %124 = bitcast i32 %123 to float
  %125 = lshr i32 %59, 16
  %126 = and i32 %59, 65535
  %127 = and i32 %60, 65535
  %128 = uitofp i32 %126 to float
  %129 = uitofp i32 %125 to float
  %130 = uitofp i32 %127 to float
  %131 = fadd fast float %128, -3.276800e+04
  %132 = fadd fast float %129, -3.276800e+04
  %133 = fadd fast float %130, -3.276800e+04
  %134 = fmul fast float %124, %131
  %135 = fmul fast float %124, %132
  %136 = fmul fast float %124, %133
  %137 = fmul fast float %109, %134
  %138 = fmul fast float %110, %134
  %139 = fmul fast float %111, %134
  %140 = fmul fast float %114, %135
  %141 = fmul fast float %117, %135
  %142 = fmul fast float %120, %135
  %143 = fmul fast float %83, %136
  %144 = fmul fast float %84, %136
  %145 = fmul fast float %136, %85
  %146 = call float @dx.op.unary.f32(i32 6, float %134)  ; FAbs(value)
  %147 = call float @dx.op.unary.f32(i32 6, float %135)  ; FAbs(value)
  %148 = call float @dx.op.unary.f32(i32 6, float %136)  ; FAbs(value)
  %149 = fdiv fast float 1.000000e+00, %146
  %150 = fdiv fast float 1.000000e+00, %147
  %151 = fdiv fast float 1.000000e+00, %148
  %152 = and i32 %43, 1
  %153 = icmp ne i32 %152, 0
  %154 = select i1 %153, float -1.000000e+00, float 1.000000e+00
  br label %155

; <label>:155                                     ; preds = %48, %27
  %156 = phi float [ %149, %48 ], [ 0.000000e+00, %27 ]
  %157 = phi float [ %150, %48 ], [ 0.000000e+00, %27 ]
  %158 = phi float [ %151, %48 ], [ 0.000000e+00, %27 ]
  %159 = phi float [ %154, %48 ], [ 0.000000e+00, %27 ]
  %160 = phi float [ %137, %48 ], [ 0.000000e+00, %27 ]
  %161 = phi float [ %138, %48 ], [ 0.000000e+00, %27 ]
  %162 = phi float [ %139, %48 ], [ 0.000000e+00, %27 ]
  %163 = phi float [ %140, %48 ], [ 0.000000e+00, %27 ]
  %164 = phi float [ %141, %48 ], [ 0.000000e+00, %27 ]
  %165 = phi float [ %142, %48 ], [ 0.000000e+00, %27 ]
  %166 = phi float [ %143, %48 ], [ 0.000000e+00, %27 ]
  %167 = phi float [ %144, %48 ], [ 0.000000e+00, %27 ]
  %168 = phi float [ %145, %48 ], [ 0.000000e+00, %27 ]
  %169 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %7, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %170 = extractvalue %dx.types.CBufRet.i32 %169, 3
  %171 = add i32 %170, %9
  %172 = shl i32 %171, 1
  %173 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 10, i32 1033 })  ; AnnotateHandle(res,props)  resource: TypedBuffer<4xF32>
  %174 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %173, i32 %172, i32 undef)  ; BufferLoad(srv,index,wot)
  %175 = extractvalue %dx.types.ResRet.f32 %174, 0
  %176 = extractvalue %dx.types.ResRet.f32 %174, 1
  %177 = extractvalue %dx.types.ResRet.f32 %174, 2
  %178 = or i32 %172, 1
  %179 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %173, i32 %178, i32 undef)  ; BufferLoad(srv,index,wot)
  %180 = extractvalue %dx.types.ResRet.f32 %179, 0
  %181 = extractvalue %dx.types.ResRet.f32 %179, 1
  %182 = extractvalue %dx.types.ResRet.f32 %179, 2
  %183 = extractvalue %dx.types.ResRet.f32 %179, 3
  %184 = fmul fast float %181, %177
  %185 = fmul fast float %182, %176
  %186 = fsub fast float %184, %185
  %187 = fmul fast float %182, %175
  %188 = fmul fast float %180, %177
  %189 = fsub fast float %187, %188
  %190 = fmul fast float %180, %176
  %191 = fmul fast float %181, %175
  %192 = fsub fast float %190, %191
  %193 = fmul fast float %186, %183
  %194 = fmul fast float %189, %183
  %195 = fmul fast float %192, %183
  %196 = fmul fast float %194, %182
  %197 = fmul fast float %195, %181
  %198 = fsub fast float %196, %197
  %199 = fmul fast float %195, %180
  %200 = fmul fast float %193, %182
  %201 = fsub fast float %199, %200
  %202 = fmul fast float %193, %181
  %203 = fmul fast float %194, %180
  %204 = fsub fast float %202, %203
  %205 = fmul fast float %198, %183
  %206 = fmul fast float %201, %183
  %207 = fmul fast float %204, %183
  %208 = fmul fast float %160, %156
  %209 = fmul fast float %161, %156
  %210 = fmul fast float %162, %156
  %211 = fmul fast float %163, %157
  %212 = fmul fast float %164, %157
  %213 = fmul fast float %165, %157
  %214 = fmul fast float %166, %158
  %215 = fmul fast float %167, %158
  %216 = fmul fast float %168, %158
  %217 = fmul fast float %205, %208
  %218 = call float @dx.op.tertiary.f32(i32 46, float %206, float %211, float %217)  ; FMad(a,b,c)
  %219 = call float @dx.op.tertiary.f32(i32 46, float %207, float %214, float %218)  ; FMad(a,b,c)
  %220 = fmul fast float %205, %209
  %221 = call float @dx.op.tertiary.f32(i32 46, float %206, float %212, float %220)  ; FMad(a,b,c)
  %222 = call float @dx.op.tertiary.f32(i32 46, float %207, float %215, float %221)  ; FMad(a,b,c)
  %223 = fmul fast float %205, %210
  %224 = call float @dx.op.tertiary.f32(i32 46, float %206, float %213, float %223)  ; FMad(a,b,c)
  %225 = call float @dx.op.tertiary.f32(i32 46, float %207, float %216, float %224)  ; FMad(a,b,c)
  %226 = fmul fast float %180, %208
  %227 = call float @dx.op.tertiary.f32(i32 46, float %181, float %211, float %226)  ; FMad(a,b,c)
  %228 = call float @dx.op.tertiary.f32(i32 46, float %182, float %214, float %227)  ; FMad(a,b,c)
  %229 = fmul fast float %180, %209
  %230 = call float @dx.op.tertiary.f32(i32 46, float %181, float %212, float %229)  ; FMad(a,b,c)
  %231 = call float @dx.op.tertiary.f32(i32 46, float %182, float %215, float %230)  ; FMad(a,b,c)
  %232 = fmul fast float %180, %210
  %233 = call float @dx.op.tertiary.f32(i32 46, float %181, float %213, float %232)  ; FMad(a,b,c)
  %234 = call float @dx.op.tertiary.f32(i32 46, float %182, float %216, float %233)  ; FMad(a,b,c)
  %235 = fmul fast float %183, %159
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %219)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %222)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %225)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float 0.000000e+00)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %228)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %231)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %234)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 3, float %235)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.i32(i32 5, i32 2, i32 0, i8 0, i32 %42)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  %236 = and i32 %9, 1
  %237 = uitofp i32 %236 to float
  %238 = lshr i32 %9, 1
  %239 = and i32 %238, 1
  %240 = uitofp i32 %239 to float
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 0, float %237)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 3, i32 0, i8 1, float %240)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.loadInput.i32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind
declare void @dx.op.storeOutput.i32(i32, i32, i32, i8, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot2.f32(i32, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

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
!dx.viewIdState = !{!16}
!dx.entryPoints = !{!17}

!0 = !{!"dxc(private) 1.8.0.0 (private, 00000000)"}
!1 = !{i32 1, i32 6}
!2 = !{i32 1, i32 8}
!3 = !{!"vs", i32 6, i32 6}
!4 = !{!5, null, !13, null}
!5 = !{!6, !8, !10, !11}
!6 = !{i32 0, %"class.StructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 4}
!8 = !{i32 1, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 1, i32 1, i32 12, i32 0, !9}
!9 = !{i32 1, i32 16}
!10 = !{i32 2, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 12, i32 0, !9}
!11 = !{i32 3, %"class.Buffer<vector<float, 4> >"* undef, !"", i32 0, i32 3, i32 1, i32 10, i32 0, !12}
!12 = !{i32 0, i32 9}
!13 = !{!14, !15}
!14 = !{i32 0, %Scene* undef, !"", i32 0, i32 0, i32 1, i32 348, null}
!15 = !{i32 1, %LocalVF* undef, !"", i32 0, i32 1, i32 1, i32 60, null}
!16 = !{[15 x i32] [i32 13, i32 14, i32 0, i32 0, i32 0, i32 0, i32 503, i32 0, i32 0, i32 0, i32 503, i32 0, i32 0, i32 0, i32 12535]}
!17 = !{void ()* @VoxelizeVS, !"VoxelizeVS", !18, !4, !34}
!18 = !{!19, !27, null}
!19 = !{!20, !22, !25, !26}
!20 = !{i32 0, !"ATTRIBUTE", i8 9, i8 0, !21, i8 0, i32 1, i8 4, i32 0, i8 0, null}
!21 = !{i32 0}
!22 = !{i32 1, !"ATTRIBUTE", i8 5, i8 0, !23, i8 0, i32 1, i8 1, i32 1, i8 0, !24}
!23 = !{i32 13}
!24 = !{i32 3, i32 1}
!25 = !{i32 2, !"SV_InstanceID", i8 5, i8 2, !21, i8 0, i32 1, i8 1, i32 2, i8 0, !24}
!26 = !{i32 3, !"SV_VertexID", i8 5, i8 1, !21, i8 0, i32 1, i8 1, i32 3, i8 0, !24}
!27 = !{!28, !30, !31, !32}
!28 = !{i32 0, !"TEXCOORD10_centroid", i8 9, i8 0, !21, i8 2, i32 1, i8 4, i32 0, i8 0, !29}
!29 = !{i32 3, i32 15}
!30 = !{i32 1, !"TEXCOORD11_centroid", i8 9, i8 0, !21, i8 2, i32 1, i8 4, i32 1, i8 0, !29}
!31 = !{i32 2, !"PRIMITIVE_ID", i8 5, i8 0, !21, i8 1, i32 1, i8 1, i32 2, i8 0, !24}
!32 = !{i32 3, !"ATTRIBUTE", i8 9, i8 0, !21, i8 2, i32 1, i8 2, i32 3, i8 0, !33}
!33 = !{i32 3, i32 3}
!34 = !{i32 0, i64 16, i32 5, !21}
!35 = distinct !{!35, !"dx.controlflow.hints", i32 1}
