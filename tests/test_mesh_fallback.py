"""A GPU without mesh shaders (RDNA1, issue 640) runs task and mesh programs as compute shaders.

The console writes a mesh program's vertices and primitives to an internal stage buffer, and Vulkan's
VK_EXT_mesh_shader hands them to the rasterizer from there. A replay only needs the records: here the
task and mesh programs are ordinary compute shaders, their epilogues write the vertices to a record
buffer, and a generated vertex stage (drawing an ordinary graphics pipeline) reads one corner a
vertex from it. This checks that path end to end in the sources: the device enables it with buffer
device addresses, the translator builds the compute stages and the generated vertex stage, the
dispatches run before the pass, and the draw is a plain vkCmdDraw over the records.

python -I -B tests/test_mesh_fallback.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VK = ROOT / "gpu" / "vk"
SHADER = VK / "vk_shader.cpp"
DRAW = VK / "vk_draw.cpp"
MAIN = VK / "vk_main.cpp"
HEADER = VK / "vk_shader.h"
EMIT = VK / "eden" / "shader_recompiler" / "backend" / "spirv" / "emit_spirv.cpp"
MESH = VK / "eden" / "shader_recompiler" / "backend" / "spirv" / "emit_spirv_mesh.cpp"
SPIRV = VK / "eden" / "shader_recompiler" / "backend" / "spirv" / "emit_spirv.h"
VMA = VK / "vk_vma.cpp"


def block(text, marker):
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    main_cpp = MAIN.read_text(encoding="utf-8")
    if "dev.mesh_fallback = !dev.mesh_shader && f12.bufferDeviceAddress" not in main_cpp:
        raise SystemExit("vk_main.cpp: no fallback without buffer device addresses")
    if "issue 640 fallback" not in main_cpp:
        raise SystemExit("vk_main.cpp: the fallback is not logged")
    if "if (!dev.mesh_shader && !dev.mesh_fallback" not in main_cpp:
        raise SystemExit("vk_main.cpp: the no-mesh-shader warning ignores the fallback")

    program = block(HEADER.read_text(encoding="utf-8"), "struct Program {")
    for field in ("bool fallback", "bool fallback_task", "fallback_topology", "fallback_vertices_per_workgroup",
                  "fallback_record_words", "fallback_payload_words", "fallback_task_threads"):
        if field not in program:
            raise SystemExit("Program has no %s" % field)

    mesh_shader = SHADER.read_text(encoding="utf-8")
    translate = block(mesh_shader, "void translate_mesh_into(")
    if "rt.mesh_fallback = fallback;" not in translate or "mrt.mesh_fallback = fallback;" not in translate:
        raise SystemExit("the task and mesh stages are not emitted as fallback stages")
    if "vrt.mesh_fallback_vertex = true;" not in translate or "vrt.mesh_fallback = true;" not in translate:
        raise SystemExit("no generated vertex stage")
    if "mesh_t->program.stage = Shader::Stage::VertexB;" not in translate:
        raise SystemExit("the generated vertex stage is not emitted as a vertex stage")
    if "mesh_t->program.info = Shader::Info{};" not in translate or \
            "mesh_t->program.info.stores = saved_info.stores;" not in translate:
        raise SystemExit("the generated vertex stage is not emitted with only the mesh program's stores")
    if "v.info = mesh_t->program.info;" in translate:
        raise SystemExit("the vertex stage still declares the mesh program's descriptors")
    if "vrt.per_primitive_generics.set(" not in translate:
        raise SystemExit("the generated vertex stage does not know the mesh program's per-primitive generics")
    if "fs->program.info.interpolation[" not in translate or "Shader::Interpolation::Flat" not in translate:
        raise SystemExit("the fallback fragment stage still declares PerPrimitiveEXT")
    if "VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_TASK_BIT_EXT" not in translate or \
            "VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_MESH_BIT_EXT" not in translate:
        raise SystemExit("the task and mesh stages are not compute stages in the fallback")
    if "p.fallback_task_threads = task_t ? task_t->program.workgroup_size[0] : 1;" not in translate:
        raise SystemExit("the task's threads are not kept for the mesh slot range")

    emit = EMIT.read_text(encoding="utf-8")
    entry = block(emit, "void DefineEntryPoint(")
    if "spv::ExecutionModel::GLCompute" not in entry or "ctx.mesh_fallback" not in entry:
        raise SystemExit("the fallback stages have no GLCompute entry point")
    if "spv::ExecutionModel::MeshEXT" not in entry or "spv::ExecutionModel::TaskEXT" not in entry:
        raise SystemExit("native mesh shaders were removed")

    mesh = MESH.read_text(encoding="utf-8")
    if "void MeshFallbackEpilogue(" not in mesh or "void EmitMeshFallbackVertexBody(" not in mesh:
        raise SystemExit("the fallback epilogue or vertex body is missing")
    if "AddLocalVariable" in block(mesh, "void Strided("):
        raise SystemExit("the strided copies declare a function variable mid-function (spirv-val rejects it)")
    task = block(mesh, "void TaskEpilogue(")
    if "ctx.OpULessThan(ctx.U1, LocalInvocation(ctx), count)" not in task:
        raise SystemExit("a task starts a mesh workgroup for every thread, not only the meshlets it kept")
    if "PushU64(ctx, MESH_FALLBACK_" in mesh:
        raise SystemExit("a push constant byte offset is used as the push block's member index")
    if "ctx.OpSelect(ctx.U32[1], started, ctx.Const(1U), ctx.u32_zero_value)" not in task:
        raise SystemExit("the task's started flag is stored as a bool into a u32 word")
    if "spv::MemoryAccessMask::Aligned, 4U" not in block(mesh, "Id LoadWord(") or \
            "spv::MemoryAccessMask::Aligned, 4U" not in block(mesh, "void StoreWord("):
        raise SystemExit("the fallback's PhysicalStorageBuffer accesses do not say their alignment")
    epilogue = block(mesh, "void MeshFallbackEpilogue(")
    if "ctx.OpAtomicIAdd" in epilogue:
        raise SystemExit("the fallback still allocates record slots atomically")
    if "ctx.mesh_fallback_records_member_index, slot" not in epilogue:
        raise SystemExit("a mesh workgroup does not write its count at its slot")
    if "MESH_FALLBACK_CAPACITY" not in epilogue:
        raise SystemExit("the records' count array is not at the fallback capacity")
    isbe = block(mesh, "Id EmitIsbeRead(")
    if "mesh_fallback_task_threads" not in isbe or "ctx.OpUMod" not in isbe:
        raise SystemExit("a fallback mesh workgroup takes its slot, not its meshlet within the task workgroup, as its index")
    if "mrt.mesh_fallback_task_threads = task_t ? task_t->program.workgroup_size[0] : 0;" not in translate:
        raise SystemExit("the mesh stage is not told its task program's threads")
    body = block(mesh, "void EmitMeshFallbackVertexBody(")
    if "ctx.OpUDiv" not in body or "ctx.OpUMod" not in body:
        raise SystemExit("the vertex stage does not take its slot, primitive and corner from VertexIndex")
    if "ctx.OpUGreaterThanEqual(ctx.U1, primitive, count)" not in body:
        raise SystemExit("a vertex past its workgroup's primitive count is not culled")
    if "per_primitive_generics" not in body or "spv::Decoration::Flat" not in body:
        raise SystemExit("the generated vertex stage does not declare its per-primitive outputs Flat")

    spirv = SPIRV.read_text(encoding="utf-8")
    if "MESH_FALLBACK_CAPACITY" not in spirv or "MESH_FALLBACK_PAYLOAD_PUSH_OFFSET" not in spirv or \
            "MESH_FALLBACK_RECORDS_PUSH_OFFSET" not in spirv:
        raise SystemExit("the fallback's push constants and capacity are not declared")
    if "MESH_FALLBACK_COUNTER_PUSH_OFFSET" in spirv:
        raise SystemExit("the removed atomic counter is still pushed")

    draw = DRAW.read_text(encoding="utf-8")
    if "bool fallback_record(VkCommandBuffer cmd, Program& p, const DrawArgs& d)" not in draw:
        raise SystemExit("vk_draw.cpp has no fallback dispatch")
    if "vkCmdDispatchIndirect(cmd, mesh_conv_buf, mesh_conv_off);" not in draw:
        raise SystemExit("an indirect mesh draw does not dispatch from its converted arguments")
    if "if (p.fallback && d.mesh && !fallback_record(cmd, p, d)) SKIP_HERE();" not in draw:
        raise SystemExit("the fallback dispatches do not run before the pass")
    if "if (fallback_vertex_count) vkCmdDraw(cmd, fallback_vertex_count, 1, 0, 0);" not in draw:
        raise SystemExit("the fallback is not drawn as an ordinary vkCmdDraw")
    if "fallback_payload_address" not in draw or "MESH_FALLBACK_PAYLOAD_PUSH_OFFSET" not in draw:
        raise SystemExit("the fallback's buffer addresses are not pushed")
    # (a payload a task did not write is cleared; only the slots this draw can start, not a whole shared pair's)
    if "fallback_payload_words * 4" not in draw or "vkCmdFillBuffer(cmd, f->payload, 0, used, 0)" not in draw:
        raise SystemExit("a payload a task did not write is not cleared before the mesh dispatch")
    # (every fallback program a recording thread draws shares one pair of buffers: per-program pairs were ~2.2 GB of VRAM
    # on an RX 5500M and the cause of the fallback's lock-up without enough of it)
    if "TLS_OBJECT(fallback_buffers, std::vector<FallbackBuffers>)" not in draw:
        raise SystemExit("the fallback still makes a record and payload pair for every program")
    if "fallback_buffers()[&p]" in draw:
        raise SystemExit("the fallback buffers are still keyed by the program")
    if "(!p.fallback || dw().prog == &p)" not in draw or "dw().prog = &p;" not in draw:
        raise SystemExit("a fallback draw reusing its descriptors after a dispatch pushes the dispatch's at the compute bind point")
    # (the pair is shared by a recording thread's programs (#1383): its clears wait for the last fallback draw's reads)
    shared = block(draw, "bool fallback_record(VkCommandBuffer cmd, Program& p, const DrawArgs& d) {")
    if "full_barrier(cmd);" not in shared[shared.index("end_pass(cmd);"):shared.index("vkCmdFillBuffer(cmd, f->records")]:
        raise SystemExit("a fallback draw clears the shared buffers before the last draw has read them")
    if "VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT" not in VMA.read_text(encoding="utf-8"):
        raise SystemExit("the allocator does not make the fallback's buffers addressable")
    if "st.vk_stage & (VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)" \
            not in draw:
        raise SystemExit("the graphics pipeline still takes the compute stages")
    # (a GPU with mesh shaders: the task and mesh stages are the graphics pipeline's own. Left out of a native mesh program,
    # its pipeline was its fragment stage alone, drew none of the grass, leaves or terrain and then lost the device)
    graphics = block(draw, "VkPipeline build_graphics(const Program& p, const NvnState& s, const Targets& at, "
                           "VkPrimitiveTopology topo, int part, bool* cold) {")
    if "if (p.fallback && (st.vk_stage & (VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT |" \
            not in graphics:
        raise SystemExit("build_graphics leaves out the task and mesh stages of native mesh programs too")
    if "dynamic_state(cmd, s, at, topo, d.mesh && !p.fallback);" not in draw:
        raise SystemExit("a fallback draw does not use the mesh program's topology")
    if "if (p.fallback) topo = p.fallback_topology;" not in draw:
        raise SystemExit("the fallback's points, lines or triangles are not used")
    if "p->mesh != dev.mesh_shader || p->fallback != dev.mesh_fallback" not in mesh_shader:
        raise SystemExit("a cached program from a GPU with the other mesh path is used")
    # (the fallback's compute pipelines are compiled on a worker, not in the draw: AMD took up to 12 s on one and the game froze
    # behind it. And before play, in the precompile, with every program translated for the fallback: white foliage until then)
    if "bool fallback_pipelines_ready(Program& p) {" not in draw:
        raise SystemExit("the fallback's compute pipelines are compiled in the draw, not on a worker (AMD: seconds each)")
    ready = block(draw, "bool fallback_pipelines_ready(Program& p) {")
    if "compare_exchange_strong(none, 1)" not in ready or "jobs_push(std::move(build), true)" not in ready or \
            "fallback_compute_pipelines(*job);" not in ready:
        raise SystemExit("the fallback's compute pipelines are compiled in the draw, not on a worker (AMD: seconds each)")
    record = block(draw, "bool fallback_record(VkCommandBuffer cmd, Program& p, const DrawArgs& d) {")
    if "fallback_pipelines_ready(p)" not in record or "fallback_compute_pipelines(p)" in record:
        raise SystemExit("a fallback draw compiles its compute pipelines itself")
    build = block(draw, "bool fallback_compute_pipelines(Program& p) {")
    if build.index("guarded_compute(") < build.index("    }\n", build.index("std::lock_guard lk(mu);")):
        raise SystemExit("the fallback's build holds its lock through the compiles (one program at a time)")
    precompile = block(draw, "void precompile_run(void (*progress)(size_t done, size_t total)) {")
    if "else if (p.fallback) fallbacks.push_back(&p);" not in precompile or "fallback_compute_pipelines(p);" not in precompile:
        raise SystemExit("the precompile leaves the mesh fallback's compute pipelines to play")
    shader = (ROOT / "gpu" / "vk" / "vk_shader.cpp").read_text(encoding="utf-8")
    if "(fallbacks && (it->second[0].kind & 4))" not in block(shader, "size_t program_cache_load_some(") or \
            "}, all_computes, true);" not in precompile:
        raise SystemExit("the precompile loads only the mesh fallback programs play has drawn (white until compiled)")
    print("mesh shader fallback (compute records and a vertex stage) ok")


if __name__ == "__main__":
    main()
