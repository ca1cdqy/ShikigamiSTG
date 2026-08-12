-- ShikigamiSTG build configuration.
-- The project uses xmake for dependency management and compilation.

-- Project metadata.
set_project("ShikigamiSTG")
set_version("0.1.0")
set_license("Apache-2.0")
-- openal-soft is LGPL-2.0; this Apache-2.0 open-source project satisfies
-- LGPL requirements (users can relink from source), so suppress the check.
set_policy("check.target_package_licenses", false)

-- Build modes.
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {lsp = "clangd"})

-- Language standard.
set_languages("c++23")

option("shiki_shared")
    set_default(false)
    set_showmenu(true)
    set_description("Build ShikigamiSTG as a shared library")
option_end()

option("asset_tests")
    set_default(true)
    set_showmenu(true)
    set_description("Build tests that require an external runtime asset package")
option_end()

-- Package dependencies.
add_requires("libsdl3", {configs = {shared = false}})
add_requires("openal-soft", {configs = {shared = true}})
add_requires("freetype", {configs = {shared = false}})
add_requires("glm")
add_requires("spdlog")
add_requires("stb")
add_requires("nlohmann_json")
add_requires("catch2")

local function configure_shiki_library()
    add_packages("libsdl3", "openal-soft", "freetype", "glm", "spdlog", "stb", "nlohmann_json")
    add_includedirs("include", {public = true})
    add_files("src/**/*.cpp")
    add_headerfiles("include/(**/*.h)")
    add_defines("SDL_MAIN_HANDLED")
    set_warnings("all")
end

    -- Deploys the dynamically linked OpenAL runtime next to a finished target
    -- so Windows can load it without a PATH change. xmake has no built-in
    -- switch that copies dependency DLLs into the output directory; this is
    -- the standard build-time deployment hook.
    local function deploy_openal_dll(target, os, path)
        local pkg = target:pkg("openal-soft")
        if pkg then
            local dir = pkg:installdir()
            if dir and os.isdir(dir) then
                for _, dll in ipairs(os.files(path.join(dir, "**", "OpenAL32.dll"))) do
                    local destination = path.join(target:targetdir(), path.filename(dll))
                    if not os.isfile(destination) then
                        os.cp(dll, destination)
                    end
                end
            end
        end
    end

-- Engine library.
target("shiki")
    if has_config("shiki_shared") then
        set_kind("shared")
        add_rules("utils.symbols.export_all")
    else
        set_kind("static")
    end
    configure_shiki_library()
    if has_config("shiki_shared") then
        after_build(function (target) deploy_openal_dll(target, os, path) end)
    end

-- Private static runtime used by the example executable. The public shiki
-- target can remain shared for SDK packaging without making the example
-- depend on a colocated framework DLL.
target("shiki_example_runtime")
    set_kind("static")
    set_default(false)
    configure_shiki_library()

-- TH06 example.
target("th06")
    set_kind("binary")
    add_deps("shiki_example_runtime")
    add_packages("libsdl3", "openal-soft", "freetype", "glm", "spdlog", "stb", "nlohmann_json")
    add_files("examples/th06/src/*.cpp")
    add_includedirs("include")
    add_defines("SDL_MAIN_HANDLED")
    set_rundir("$(builddir)/$(plat)/$(arch)/$(mode)")
    after_build(function (target) deploy_openal_dll(target, os, path) end)

    -- Compile shaders for the host GPU backend.
    before_build(function (target)
        import("net.http")

        local dxc_dir = path.join(os.projectdir(), "build", "dxc")
        local shader_dir = path.join(os.projectdir(), "assets", "shaders")
        local output_dir = path.join(target:targetdir(), "shaders")
        os.mkdir(output_dir)
        local vert_src = path.join(shader_dir, "sprite.vert.hlsl")
        local frag_src = path.join(shader_dir, "sprite.frag.hlsl")

        local function download_dxc()
            local dxc
            if is_host("windows") then
                dxc = path.join(dxc_dir, "bin", "x64", "dxc.exe")
            else
                dxc = path.join(dxc_dir, "bin", "dxc")
            end
            local function find_dxc()
                if os.isfile(dxc) then
                    return dxc
                end
                local pattern = is_host("windows") and "**/dxc.exe" or
                                    "**/dxc"
                for _, candidate in ipairs(os.files(path.join(dxc_dir, pattern))) do
                    if os.isfile(candidate) then
                        return candidate
                    end
                end
                return nil
            end
            local existing = find_dxc()
            if existing then
                return existing
            end

            local version = "1.8.2407"
            local archive
            local url
            if is_host("windows") then
                archive = path.join(dxc_dir, "dxc-windows.zip")
                url = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v" ..
                      version .. "/dxc_2024_07_31.zip"
            else
                archive = path.join(dxc_dir, "dxc-linux.tar.gz")
                url = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v" ..
                      version .. "/linux_dxc_2024_07_31.x86_64.tar.gz"
            end
            local override = os.getenv("SHIKI_DXC_URL")
            if override and #override > 0 then
                url = override
            end
            os.mkdir(dxc_dir)
            print("Downloading DXC for " .. os.host() .. "...")
            http.download(url, archive)
            if is_host("windows") then
                os.run("powershell -Command \"Expand-Archive -Path '" ..
                       archive .. "' -DestinationPath '" .. dxc_dir ..
                       "' -Force\"")
            else
                os.runv("tar", {"-xzf", archive, "-C", dxc_dir})
            end
            os.rm(archive)
            local downloaded = find_dxc()
            if not downloaded then
                raise("DXC download did not provide an executable under: " ..
                      dxc_dir)
            end
            return downloaded
        end

        local function compile_dxc(dxc, source, profile, output, spirv)
            local args = {"-T", profile, "-E", "main", "-Fo", output}
            if spirv then
                table.insert(args, "-spirv")
                table.insert(args, "-fspv-target-env=vulkan1.2")
            end
            table.insert(args, source)
            os.runv(dxc, args)
            print("Compiled shader: " .. output)
        end

        if is_host("macosx") then
            local function compile_metal(source, output, entry)
                local air = output .. ".air"
                os.runv("xcrun", {"-sdk", "macosx", "metal", "-c", source,
                                   "-o", air})
                os.runv("xcrun", {"-sdk", "macosx", "metallib", air, "-o",
                                   output})
                os.rm(air)
                print("Compiled Metal library: " .. output .. " (" .. entry .. ")")
            end
            compile_metal(path.join(shader_dir, "sprite.vert.metal"),
                          path.join(output_dir, "sprite.vert.metallib"),
                          "spriteVertex")
            compile_metal(path.join(shader_dir, "sprite.frag.metal"),
                          path.join(output_dir, "sprite.frag.metallib"),
                          "spriteFragment")
        else
            local dxc = download_dxc()
            if is_host("windows") then
                compile_dxc(dxc, vert_src, "vs_6_0",
                            path.join(output_dir, "sprite.vert.dxil"), false)
                compile_dxc(dxc, frag_src, "ps_6_0",
                            path.join(output_dir, "sprite.frag.dxil"), false)
            end
            compile_dxc(dxc, vert_src, "vs_6_0",
                        path.join(output_dir, "sprite.vert.spv"), true)
            compile_dxc(dxc, frag_src, "ps_6_0",
                        path.join(output_dir, "sprite.frag.spv"), true)
        end
    end)

-- Standalone public-API pattern example with an SDL3 presentation frontend.
target("wave_particle")
    set_kind("binary")
    add_deps("shiki_example_runtime")
    add_packages("libsdl3", "openal-soft", "freetype", "glm", "spdlog", "stb", "nlohmann_json")
    add_files("examples/wave_particle/src/*.cpp")
    add_includedirs("include")
    add_defines("SDL_MAIN_HANDLED")
    set_rundir("$(builddir)/$(plat)/$(arch)/$(mode)")
    after_build(function (target) deploy_openal_dll(target, os, path) end)

-- Add the test target when test sources are present.
if os.isdir("tests") and #os.files("tests/*.cpp") > 0 then
    target("tests")
        set_kind("binary")
        add_deps("shiki")
        add_packages("catch2", "libsdl3", "openal-soft", "freetype", "glm", "spdlog", "stb", "nlohmann_json")
        add_files("tests/*.cpp")
        if not has_config("asset_tests") then
            remove_files("tests/test_ecl_runtime.cpp")
        end
        add_includedirs("include", "examples/th06/src")
        add_defines("SDL_MAIN_HANDLED")
        set_rundir("$(builddir)/$(plat)/$(arch)/$(mode)")
        after_build(function (target) deploy_openal_dll(target, os, path) end)
end

task("docs")
    set_menu {
        usage = "xmake docs",
        description = "Build complete documentation to build/docs/ (docsify + Doxygen)"
    }
    on_run(function ()
        local output_dir = path.join(os.projectdir(), "build", "docs")

        -- This directory is exclusively generated by this task. Rebuild it to
        -- prevent deleted Docsify pages or old Doxygen files from lingering.
        os.rm(output_dir)
        os.mkdir(output_dir)
        os.cp(path.join("docs", "*"), output_dir)

        -- Docsify is a runtime Markdown site generator.  Its CLI generates
        -- navigation metadata in the output tree while the checked-in
        -- index.html remains the application entry point.
        local docsify = path.join(os.projectdir(), "docs", "node_modules",
                                  ".bin", "docsify.cmd")
        if not os.isfile(docsify) then
            local npm_prefix = os.getenv("APPDATA")
            if npm_prefix then
                local global_docsify = path.join(npm_prefix, "npm",
                                                 "docsify.cmd")
                if os.isfile(global_docsify) then
                    docsify = global_docsify
                else
                    docsify = nil
                end
            else
                docsify = nil
            end
        end
        if not docsify then
            raise("Docsify CLI was not found. Run `npm install` in docs/ " ..
                  "or install docsify-cli globally before running `xmake docs`.")
        end
        -- `docsify generate` refuses to overwrite an existing sidebar. The
        -- generated copy is disposable; the source remains under docs/.
        os.rm(path.join(output_dir, "_sidebar.md"))
        os.execv(docsify, {"generate", output_dir})

        -- Use xmake's Doxygen plugin so the generated reference inherits the
        -- project metadata declared by set_project() and set_version(). The
        -- output path itself is configured in Doxyfile.
        os.execv("xmake", {"doxygen"})
        print("Documentation built to build/docs/.")
        print("Open build/docs/index.html in a browser to preview.")
    end)

-- Engine micro-benchmarks. Use a release build for meaningful numbers:
-- `xmake f -m release && xmake build bench && xmake run bench`.
target("bench")
    set_kind("binary")
    add_deps("shiki")
    add_packages("libsdl3", "openal-soft", "freetype", "glm", "spdlog", "stb", "nlohmann_json")
    add_files("bench/*.cpp")
    add_includedirs("include")
    add_defines("SDL_MAIN_HANDLED")
    set_rundir("$(builddir)/$(plat)/$(arch)/$(mode)")