/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Benchmark.h"
#include "SkCanvas.h"
#include "SkImageEncoder.h"
#if SK_SUPPORT_GPU
#include "GrTest.h"
#include "gl/GrGLGLSL.h"
#include "gl/GrGLInterface.h"
#include "gl/GrGLShaderVar.h"
#include "gl/GrGLUtil.h"
#include "glsl/GrGLSLCaps.h"
#include <stdio.h>

/*
 * This is a native GL benchmark for determining the cost of uploading vertex attributes
 */
class GLVertexAttributesBench : public Benchmark {
public:
    GLVertexAttributesBench(uint32_t attribs)
        : fTexture(0)
        , fBuffers(0)
        , fProgram(0)
        , fVAO(0)
        , fVBO(0)
        , fAttribs(attribs)
        , fStride(2 * sizeof(SkPoint) + fAttribs * sizeof(GrGLfloat) * 4) {
        fName.appendf("GLVertexAttributesBench_%d", fAttribs);
    }

protected:
    const char* onGetName() override { return fName.c_str(); }
    void onPerCanvasPreDraw(SkCanvas* canvas) override;
    void setup(const GrGLContext*);
    void onDraw(const int loops, SkCanvas*) override;
    void onPerCanvasPostDraw(SkCanvas* canvas) override;

    static const GrGLuint kScreenWidth = 800;
    static const GrGLuint kScreenHeight = 600;
    static const uint32_t kNumTri = 10000;
    static const uint32_t kVerticesPerTri = 3;
    static const uint32_t kDrawMultiplier = 512;
    static const uint32_t kMaxAttribs = 7;

private:
    GrGLuint fTexture;
    SkTArray<GrGLuint> fBuffers;
    GrGLuint fProgram;
    GrGLuint fVAO;
    GrGLuint fVBO;
    SkTArray<unsigned char> fVertices;
    uint32_t fAttribs;
    size_t fStride;
    SkString fName;
    typedef Benchmark INHERITED;
};

static const GrGLContext* get_gl_context(SkCanvas* canvas) {
    // This bench exclusively tests GL calls directly
    if (NULL == canvas->getGrContext()) {
        return NULL;
    }
    GrContext* context = canvas->getGrContext();

    GrTestTarget tt;
    context->getTestTarget(&tt);
    if (!tt.target()) {
        SkDebugf("Couldn't get Gr test target.");
        return NULL;
    }

    const GrGLContext* ctx = tt.glContext();
    if (!ctx) {
        SkDebugf("Couldn't get an interface\n");
        return NULL;
    }

    return ctx;
}

void GLVertexAttributesBench::onPerCanvasPreDraw(SkCanvas* canvas) {
    // This bench exclusively tests GL calls directly
    const GrGLContext* ctx = get_gl_context(canvas);
    if (!ctx) {
        return;
    }
    this->setup(ctx);
}

void GLVertexAttributesBench::onPerCanvasPostDraw(SkCanvas* canvas) {
    // This bench exclusively tests GL calls directly
    const GrGLContext* ctx = get_gl_context(canvas);
    if (!ctx) {
        return;
    }

    const GrGLInterface* gl = ctx->interface();

    // teardown
    GR_GL_CALL(gl, BindBuffer(GR_GL_ARRAY_BUFFER, 0));
    GR_GL_CALL(gl, BindVertexArray(0));
    GR_GL_CALL(gl, BindTexture(GR_GL_TEXTURE_2D, 0));
    GR_GL_CALL(gl, BindFramebuffer(GR_GL_FRAMEBUFFER, 0));
    GR_GL_CALL(gl, DeleteTextures(1, &fTexture));
    GR_GL_CALL(gl, DeleteProgram(fProgram));
    GR_GL_CALL(gl, DeleteBuffers(fBuffers.count(), fBuffers.begin()));
    GR_GL_CALL(gl, DeleteVertexArrays(1, &fVAO));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static GrGLuint load_shader(const GrGLInterface* gl, const char* shaderSrc, GrGLenum type) {
    GrGLuint shader;
    // Create the shader object
    GR_GL_CALL_RET(gl, shader, CreateShader(type));

    // Load the shader source
    GR_GL_CALL(gl, ShaderSource(shader, 1, &shaderSrc, NULL));

    // Compile the shader
    GR_GL_CALL(gl, CompileShader(shader));

    // Check for compile time errors
    GrGLint success;
    GrGLchar infoLog[512];
    GR_GL_CALL(gl, GetShaderiv(shader, GR_GL_COMPILE_STATUS, &success));
    if (!success) {
        GR_GL_CALL(gl, GetShaderInfoLog(shader, 512, NULL, infoLog));
        SkDebugf("ERROR::SHADER::COMPLIATION_FAILED: %s\n", infoLog);
    }

    return shader;
}

static GrGLuint compile_shader(const GrGLContext* ctx, uint32_t attribs, uint32_t maxAttribs) {
    const char* version = GrGLGetGLSLVersionDecl(*ctx);

    // setup vertex shader
    GrGLShaderVar aPosition("a_position", kVec4f_GrSLType, GrShaderVar::kAttribute_TypeModifier);
    SkTArray<GrGLShaderVar> aVars;
    SkTArray<GrGLShaderVar> oVars;

    SkString vshaderTxt(version);
    aPosition.appendDecl(*ctx, &vshaderTxt);
    vshaderTxt.append(";\n");

    for (uint32_t i = 0; i < attribs; i++) {
        SkString aname;
        aname.appendf("a_color_%d", i);
        aVars.push_back(GrGLShaderVar(aname.c_str(),
                                       kVec4f_GrSLType,
                                       GrShaderVar::kAttribute_TypeModifier));
        aVars.back().appendDecl(*ctx, &vshaderTxt);
        vshaderTxt.append(";\n");

    }

    for (uint32_t i = 0; i < maxAttribs; i++) {
        SkString oname;
        oname.appendf("o_color_%d", i);
        oVars.push_back(GrGLShaderVar(oname.c_str(),
                                       kVec4f_GrSLType,
                                       GrShaderVar::kVaryingOut_TypeModifier));
        oVars.back().appendDecl(*ctx, &vshaderTxt);
        vshaderTxt.append(";\n");
    }

    vshaderTxt.append(
            "void main()\n"
            "{\n"
                "gl_Position = a_position;\n");

    for (uint32_t i = 0; i < attribs; i++) {
        vshaderTxt.appendf("%s = %s;\n", oVars[i].c_str(), aVars[i].c_str());
    }

    // Passthrough position as a dummy
    for (uint32_t i = attribs; i < maxAttribs; i++) {
        vshaderTxt.appendf("%s = vec4(0.f, 0.f, 0.f, 1.f);\n", oVars[i].c_str());
    }

    vshaderTxt.append("}\n");

    const GrGLInterface* gl = ctx->interface();
    GrGLuint vertexShader = load_shader(gl, vshaderTxt.c_str(), GR_GL_VERTEX_SHADER);

    // setup fragment shader
    GrGLShaderVar oFragColor("o_FragColor", kVec4f_GrSLType, GrShaderVar::kOut_TypeModifier);
    SkString fshaderTxt(version);
    GrGLAppendGLSLDefaultFloatPrecisionDeclaration(kDefault_GrSLPrecision, gl->fStandard,
                                                   &fshaderTxt);

    const char* fsOutName;
    if (ctx->caps()->glslCaps()->mustDeclareFragmentShaderOutput()) {
        oFragColor.appendDecl(*ctx, &fshaderTxt);
        fshaderTxt.append(";\n");
        fsOutName = oFragColor.c_str();
    } else {
        fsOutName = "gl_FragColor";
    }

    for (uint32_t i = 0; i < maxAttribs; i++) {
        oVars[i].setTypeModifier(GrShaderVar::kVaryingIn_TypeModifier);
        oVars[i].appendDecl(*ctx, &fshaderTxt);
        fshaderTxt.append(";\n");
    }

    fshaderTxt.appendf(
            "void main()\n"
            "{\n"
               "%s = ", fsOutName);

    fshaderTxt.appendf("%s", oVars[0].c_str());
    for (uint32_t i = 1; i < maxAttribs; i++) {
        fshaderTxt.appendf(" + %s", oVars[i].c_str());
    }

    fshaderTxt.append(";\n"
                      "}\n");

    GrGLuint fragmentShader = load_shader(gl, fshaderTxt.c_str(), GR_GL_FRAGMENT_SHADER);

    GrGLint shaderProgram;
    GR_GL_CALL_RET(gl, shaderProgram, CreateProgram());
    GR_GL_CALL(gl, AttachShader(shaderProgram, vertexShader));
    GR_GL_CALL(gl, AttachShader(shaderProgram, fragmentShader));
    GR_GL_CALL(gl, LinkProgram(shaderProgram));

    // Check for linking errors
    GrGLint success;
    GrGLchar infoLog[512];
    GR_GL_CALL(gl, GetProgramiv(shaderProgram, GR_GL_LINK_STATUS, &success));
    if (!success) {
        GR_GL_CALL(gl, GetProgramInfoLog(shaderProgram, 512, NULL, infoLog));
        SkDebugf("Linker Error: %s\n", infoLog);
    }
    GR_GL_CALL(gl, DeleteShader(vertexShader));
    GR_GL_CALL(gl, DeleteShader(fragmentShader));

    return shaderProgram;
}

//#define DUMP_IMAGES
#ifdef DUMP_IMAGES
static void dump_image(const GrGLInterface* gl, uint32_t screenWidth, uint32_t screenHeight,
                       const char* filename) {
    // read back pixels
    uint32_t readback[screenWidth * screenHeight];
    GR_GL_CALL(gl, ReadPixels(0, // x
                              0, // y
                              screenWidth, // width
                              screenHeight, // height
                              GR_GL_RGBA, //format
                              GR_GL_UNSIGNED_BYTE, //type
                              readback));

    // dump png
    SkBitmap bm;
    if (!bm.tryAllocPixels(SkImageInfo::MakeN32Premul(screenWidth, screenHeight))) {
        SkDebugf("couldn't allocate bitmap\n");
        return;
    }

    bm.setPixels(readback);

    if (!SkImageEncoder::EncodeFile(filename, bm, SkImageEncoder::kPNG_Type, 100)) {
        SkDebugf("------ failed to encode %s\n", filename);
        remove(filename);   // remove any partial file
        return;
    }
}
#endif

static void setup_framebuffer(const GrGLInterface* gl, int screenWidth, int screenHeight) {
    //Setup framebuffer
    GrGLuint texture;
    GR_GL_CALL(gl, PixelStorei(GR_GL_UNPACK_ROW_LENGTH, 0));
    GR_GL_CALL(gl, PixelStorei(GR_GL_PACK_ROW_LENGTH, 0));
    GR_GL_CALL(gl, GenTextures(1, &texture));
    GR_GL_CALL(gl, ActiveTexture(GR_GL_TEXTURE15));
    GR_GL_CALL(gl, BindTexture(GR_GL_TEXTURE_2D, texture));
    GR_GL_CALL(gl, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_MAG_FILTER, GR_GL_NEAREST));
    GR_GL_CALL(gl, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_MIN_FILTER, GR_GL_NEAREST));
    GR_GL_CALL(gl, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_WRAP_S, GR_GL_CLAMP_TO_EDGE));
    GR_GL_CALL(gl, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_WRAP_T, GR_GL_CLAMP_TO_EDGE));
    GR_GL_CALL(gl, TexImage2D(GR_GL_TEXTURE_2D,
                              0, //level
                              GR_GL_RGBA8, //internal format
                              screenWidth, // width
                              screenHeight, // height
                              0, //border
                              GR_GL_RGBA, //format
                              GR_GL_UNSIGNED_BYTE, // type
                              NULL));

    // bind framebuffer
    GrGLuint framebuffer;
    GR_GL_CALL(gl, BindTexture(GR_GL_TEXTURE_2D, 0));
    GR_GL_CALL(gl, GenFramebuffers(1, &framebuffer));
    GR_GL_CALL(gl, BindFramebuffer(GR_GL_FRAMEBUFFER, framebuffer));
    GR_GL_CALL(gl, FramebufferTexture2D(GR_GL_FRAMEBUFFER,
                                        GR_GL_COLOR_ATTACHMENT0,
                                        GR_GL_TEXTURE_2D,
                                        texture, 0));
    GR_GL_CALL(gl, CheckFramebufferStatus(GR_GL_FRAMEBUFFER));
    GR_GL_CALL(gl, Viewport(0, 0, screenWidth, screenHeight));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void GLVertexAttributesBench::setup(const GrGLContext* ctx) {
    const GrGLInterface* gl = ctx->interface();
    setup_framebuffer(gl, kScreenWidth, kScreenHeight);

    fProgram = compile_shader(ctx, fAttribs, kMaxAttribs);

    // setup matrices
    SkMatrix viewMatrices[kNumTri];
    for (uint32_t i = 0 ; i < kNumTri; i++) {
        SkMatrix m = SkMatrix::I();
        m.setScale(0.0001f, 0.0001f);
        viewMatrices[i] = m;
    }

    // setup VAO
    GR_GL_CALL(gl, GenVertexArrays(1, &fVAO));
    GR_GL_CALL(gl, BindVertexArray(fVAO));

    // presetup vertex attributes, color is set to be a light gray no matter how many vertex
    // attributes are used
    float targetColor = 0.9f;
    float colorContribution = targetColor / fAttribs;
    fVertices.reset(static_cast<int>(kVerticesPerTri * kNumTri * fStride));
    for (uint32_t i = 0; i < kNumTri; i++) {
        unsigned char* ptr = &fVertices[static_cast<int>(i * kVerticesPerTri * fStride)];
        SkPoint* p = reinterpret_cast<SkPoint*>(ptr);
        p->set(-1.0f, -1.0f); p++; p->set( 0.0f, 1.0f);
        p = reinterpret_cast<SkPoint*>(ptr + fStride);
        p->set( 1.0f, -1.0f); p++; p->set( 0.0f, 1.0f);
        p = reinterpret_cast<SkPoint*>(ptr + fStride * 2);
        p->set( 1.0f,  1.0f); p++; p->set( 0.0f, 1.0f);

        SkPoint* position = reinterpret_cast<SkPoint*>(ptr);
        viewMatrices[i].mapPointsWithStride(position, fStride, kVerticesPerTri);

        // set colors
        for (uint32_t j = 0; j < kVerticesPerTri; j++) {
            GrGLfloat* f = reinterpret_cast<GrGLfloat*>(ptr + 2 * sizeof(SkPoint) + fStride * j);
            for (uint32_t k = 0; k < fAttribs * 4; k += 4) {
                f[k] = colorContribution;
                f[k + 1] = colorContribution;
                f[k + 2] = colorContribution;
                f[k + 3] = 1.0f;
            }
        }
    }

    GR_GL_CALL(gl, GenBuffers(1, &fVBO));
    fBuffers.push_back(fVBO);

    // clear screen
    GR_GL_CALL(gl, ClearColor(0.03f, 0.03f, 0.03f, 1.0f));
    GR_GL_CALL(gl, Clear(GR_GL_COLOR_BUFFER_BIT));

    // set us up to draw
    GR_GL_CALL(gl, UseProgram(fProgram));
    GR_GL_CALL(gl, BindVertexArray(fVAO));
}

void GLVertexAttributesBench::onDraw(const int loops, SkCanvas* canvas) {
    const GrGLContext* ctx = get_gl_context(canvas);
    if (!ctx) {
        return;
    }

    const GrGLInterface* gl = ctx->interface();

    // upload vertex attributes
    GR_GL_CALL(gl, BindBuffer(GR_GL_ARRAY_BUFFER, fVBO));
    GR_GL_CALL(gl, EnableVertexAttribArray(0));
    GR_GL_CALL(gl, VertexAttribPointer(0, 4, GR_GL_FLOAT, GR_GL_FALSE, (GrGLsizei)fStride,
                                       (GrGLvoid*)0));

    size_t runningStride = 2 * sizeof(SkPoint);
    for (uint32_t i = 0; i < fAttribs; i++) {
        int attribId = i + 1;
        GR_GL_CALL(gl, EnableVertexAttribArray(attribId));
        GR_GL_CALL(gl, VertexAttribPointer(attribId, 4, GR_GL_FLOAT, GR_GL_FALSE,
                                           (GrGLsizei)fStride, (GrGLvoid*)(runningStride)));
        runningStride += sizeof(GrGLfloat) * 4;
    }

    GR_GL_CALL(gl, BufferData(GR_GL_ARRAY_BUFFER, fVertices.count(), fVertices.begin(),
                              GR_GL_STREAM_DRAW));

    uint32_t maxTrianglesPerFlush = kNumTri;
    uint32_t trianglesToDraw = loops * kDrawMultiplier;

    while (trianglesToDraw > 0) {
        uint32_t triangles = SkTMin(trianglesToDraw, maxTrianglesPerFlush);
        GR_GL_CALL(gl, DrawArrays(GR_GL_TRIANGLES, 0, kVerticesPerTri * triangles));
        trianglesToDraw -= triangles;
    }

#ifdef DUMP_IMAGES
    //const char* filename = "/data/local/tmp/out.png";
    SkString filename("out");
    filename.appendf("_%s.png", this->getName());
    dump_image(gl, kScreenWidth, kScreenHeight, filename.c_str());
#endif
}


///////////////////////////////////////////////////////////////////////////////

DEF_BENCH( return new GLVertexAttributesBench(0) )
DEF_BENCH( return new GLVertexAttributesBench(1) )
DEF_BENCH( return new GLVertexAttributesBench(2) )
DEF_BENCH( return new GLVertexAttributesBench(3) )
DEF_BENCH( return new GLVertexAttributesBench(4) )
DEF_BENCH( return new GLVertexAttributesBench(5) )
DEF_BENCH( return new GLVertexAttributesBench(6) )
DEF_BENCH( return new GLVertexAttributesBench(7) )
#endif