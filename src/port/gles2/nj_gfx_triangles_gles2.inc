/* Private implementation included by nj_gfx_gles2.c. No borrowed pointer is
 * stored here: only owned CPU arrays and private-context GL object names. */
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

typedef struct triangle_vertex_gpu {
    GLfloat position[2], depth, uvq[3], color[4];
} triangle_vertex_gpu;
typedef struct triangle_state {
    GLuint seed_program, draw_program, shaders[4], depth, keys;
    GLuint *textures;
    texture_storage *slot_storage;
    texture_storage keys_storage;
    int depth_width, depth_height;
    size_t texture_capacity, texture_count;
    triangle_vertex_gpu *vertices;
    size_t vertices_size;
    GLint seed_image, seed_shift;
    GLint image, textured, wrapping, function, threshold, fog, fog_color, pass, blend;
    int ready;
} triangle_state;
typedef struct triangle_view {
    const nj_u1 *pixels;
    int width, height, bpp;
} triangle_view;

static const char triangle_depth_prefix[] =
    "#ifdef GL_ES\n#extension GL_EXT_frag_depth : require\n"
    "precision highp float;\n#define NJ_DEPTH gl_FragDepthEXT\n"
    "#else\n#define NJ_DEPTH gl_FragDepth\n#endif\n";
static const char triangle_pack_source[] =
    "float depthRank(float z){float p=clamp(z,0.0,1.0)*16777215.0;"
    "return floor(p)+step(0.5,fract(p));}"
    /* Avoid an approximate reciprocal of 2^24-1 (off by a full D24 rank on
     * NVIDIA). Two exact power-of-two scales round to the canonical ratio. */
    "float rankWindow(float q){float z=q*0.000000059604644775390625;"
    "return z+z*0.000000059604644775390625;}"
    "vec4 packRank(float q){float k=16777216.0-q;"
    "return vec4(floor(k/16777216.0),floor(mod(k,16777216.0)/65536.0),"
    "floor(mod(k,65536.0)/256.0),mod(k,256.0))/255.0; }";
static const char triangle_seed_vs[] =
    "attribute vec2 position; attribute vec2 texcoord; varying highp vec2 uv;"
    "void main(){uv=texcoord;gl_Position=vec4(position,0.0,1.0);}";
static const char triangle_seed_fs[] =
    "varying highp vec2 uv; uniform highp sampler2D image; uniform float shift;"
    "void main(){vec4 b=floor(texture2D(image,uv)*255.0+0.5);"
    "float k=((b.r*256.0+b.g)*256.0+b.b)*256.0+b.a;"
    "float q=clamp(16777216.0-max(k,1.0)+shift,0.0,16777215.0);"
    "NJ_DEPTH=rankWindow(q);gl_FragColor=packRank(q);}";
static const char triangle_draw_vs[] =
    "attribute vec2 position;attribute float windowDepth;attribute vec3 texcoord;"
    "attribute vec4 color;varying highp float z;varying highp vec3 uvq;"
    "varying highp vec4 tint;void main(){z=windowDepth;uvq=texcoord;tint=color;"
    "gl_Position=vec4(position,0.0,1.0);}";
static const char triangle_draw_fs[] =
    "varying highp float z;varying highp vec3 uvq;varying highp vec4 tint;"
    "uniform highp sampler2D image;uniform bool textured;uniform vec2 wrapping;"
    "uniform vec2 function;uniform float threshold;uniform vec4 fog;"
    "uniform vec3 fogColor;uniform bool depthPass;uniform int blend;"
    "void main(){vec4 c=clamp(tint,0.0,1.0);"
    "if(textured){vec2 uv=uvq.xy/uvq.z;"
    "uv=vec2(wrapping.x>0.5?fract(uv.x):clamp(uv.x,0.0,1.0),"
    "wrapping.y>0.5?fract(uv.y):clamp(uv.y,0.0,1.0));"
    "vec4 t=texture2D(image,uv);float a=c.a;"
    "if(function.x==226.0||(function.x==228.0&&function.y>0.5))c.a=a;"
    "else if(function.x==228.0)c.a=t.a;else c.a=a*t.a;"
    "if(function.x==226.0||function.x==228.0)c.rgb=t.rgb;else c.rgb*=t.rgb;}"
    /* Match the integer threshold domain, including threshold 128 for 0.5. */
    "if(floor(c.a*255.0+0.5)<threshold)discard;"
    "float q=depthRank(z);NJ_DEPTH=rankWindow(q);"
    "if(depthPass){gl_FragColor=packRank(q);return;}"
    "if(fog.x>0.5){float dist=1.0/uvq.z;float f;"
    "if(fog.x==81.0)f=fog.z>fog.y?(fog.z-dist)/(fog.z-fog.y):1.0;"
    "else f=exp(-fog.w*dist);c.rgb=mix(fogColor,c.rgb,clamp(f,0.0,1.0));}"
    "if(blend==65){c.rgb*=c.a;c.a=1.0;}"
    "else if(blend==66||blend==67)c.a=1.0;gl_FragColor=c;}";

static void triangles_delete(nj_gles2 *s)
{
    triangle_state *t=s->geometry;
    size_t i;
    if (!t) return;
#if NJ_GLES2_PROFILE
    tri_prof_print("final");
#endif
    if (t->seed_program) s->gl.DeleteProgram(t->seed_program);
    if (t->draw_program) s->gl.DeleteProgram(t->draw_program);
    for (i=0;i<4;++i) if (t->shaders[i]) s->gl.DeleteShader(t->shaders[i]);
    if (t->depth) s->gl.DeleteRenderbuffers(1,&t->depth);
    if (t->keys) s->gl.DeleteTextures(1,&t->keys);
    if (s->tri_session_fbo) s->gl.DeleteFramebuffers(1,&s->tri_session_fbo);
    s->tri_session_fbo=0;s->tri_session=0;
    if (t->texture_count) s->gl.DeleteTextures((GLsizei)t->texture_count,t->textures);
    t->seed_program=t->draw_program=t->depth=t->keys=0;
    memset(t->shaders,0,sizeof t->shaders);
    t->texture_count=0;t->ready=0;
    memset(&t->keys_storage,0,sizeof t->keys_storage);
    t->depth_width=t->depth_height=0;
}
static void triangles_abandon(nj_gles2 *s)
{
    if (s->geometry) {
        free(s->geometry->textures);free(s->geometry->slot_storage);free(s->geometry->vertices);
        free(s->geometry);s->geometry=NULL;
    }
}
static GLuint triangle_shader(nj_gles2 *s, GLenum type, const char *body)
{
    char *text;
    size_t a=strlen(triangle_depth_prefix),b=strlen(triangle_pack_source),c=strlen(body);
    GLuint id;
    if (type==GL_VERTEX_SHADER) return shader(s,type,body);
    text=(char *)malloc(a+b+c+1u);
    if (!text) return 0;
    memcpy(text,triangle_depth_prefix,a);memcpy(text+a,triangle_pack_source,b);
    memcpy(text+a+b,body,c+1u);
    id=shader(s,type,text);free(text);return id;
}
static int triangle_link(nj_gles2 *s, GLuint p, GLuint v, GLuint f, int seed)
{
    GLint ok=0;
    s->gl.AttachShader(p,v);s->gl.AttachShader(p,f);
    s->gl.BindAttribLocation(p,0,"position");
    if (seed) s->gl.BindAttribLocation(p,1,"texcoord");
    else {
        s->gl.BindAttribLocation(p,1,"windowDepth");
        s->gl.BindAttribLocation(p,2,"texcoord");
        s->gl.BindAttribLocation(p,3,"color");
    }
    s->gl.LinkProgram(p);s->gl.GetProgramiv(p,GL_LINK_STATUS,&ok);return ok!=0;
}
static int triangle_extension(const char *list,const char *name)
{
    size_t n=strlen(name);
    const char *p=list;
    if (!p) return 0;
    while ((p=strstr(p,name))!=NULL) {
        if ((p==list||p[-1]==' ')&&(p[n]==0||p[n]==' ')) return 1;
        p+=n;
    }
    return 0;
}
static void triangle_quad(nj_gles2 *s)
{
    static const GLfloat xy[8]={-1,-1,1,-1,-1,1,1,1};
    static const GLfloat uv[8]={0,0,1,0,0,1,1,1};
    s->gl.BindBuffer(GL_ARRAY_BUFFER,0);
    s->gl.EnableVertexAttribArray(0);s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,xy);
    s->gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP,0,4);
    s->gl.DisableVertexAttribArray(0);s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);
    s->gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,NULL);
}
static int triangle_storage(nj_gles2 *s,int w,int h)
{
    GLint bits=0;
    triangle_state *t=s->geometry;
    s->gl.BindRenderbuffer(GL_RENDERBUFFER,t->depth);
    if(t->depth_width!=w||t->depth_height!=h) {
        t->depth_width=t->depth_height=0;
        s->gl.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
        s->gl.GetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_DEPTH_SIZE,&bits);
        if(bits!=24||!clean_errors(s)||!current(s))return 0;
        t->depth_width=w;t->depth_height=h;
    }
    s->gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,t->depth);
    if (s->config.desktop_gl) s->gl.DepthRange(0.0,1.0);
    else s->gl.DepthRangef(0.0f,1.0f);
    s->gl.Disable(GL_POLYGON_OFFSET_FILL);
    return s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE&&clean_errors(s);
}
/* Gate D24 rank fidelity with actual raster comparisons, not just attachment
 * capability strings. The two saturated endpoints intentionally share rank. */
static int triangle_depth_gate(nj_gles2 *s)
{
    static const nj_u4 keys[]={0,1,2,255,256,257,65535,65536,65537,
                              8388607,8388608,8388609,16777214,16777215,16777216};
    enum {N=sizeof keys/sizeof keys[0]};
    nj_u1 input[N*4],output[N*4];
    triangle_state *t=s->geometry;
    size_t i;
    int step;
    if (!prepare(s,N,1)||!triangle_storage(s,N,1)) return 0;
    for(i=0;i<N;++i) pack_key(input+i*4,keys[i]);
    texture(s,s->source);
    if(!texture_upload(s,&s->source_storage,N,1,GL_RGBA,input))return 0;
    s->gl.UseProgram(t->seed_program);s->gl.Uniform1i(t->seed_image,0);
    s->gl.Uniform1f(t->seed_shift,0.0f);
    s->gl.Enable(GL_DEPTH_TEST);s->gl.DepthFunc(GL_ALWAYS);s->gl.DepthMask(GL_TRUE);
    s->gl.ColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);triangle_quad(s);
    s->gl.DepthMask(GL_FALSE);s->gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    for(step=0;step<3;++step) {
        s->gl.ClearColor(1,1,1,1);s->gl.Clear(GL_COLOR_BUFFER_BIT);
        s->gl.DepthFunc(step==0?GL_EQUAL:step==1?GL_LEQUAL:GL_GEQUAL);
        s->gl.Uniform1f(t->seed_shift,step==0?0.0f:step==1?1.0f:-1.0f);
        triangle_quad(s);
        s->gl.ReadPixels(0,0,N,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
        if (!clean_errors(s)||!current(s)) return 0;
        for(i=0;i<N;++i) {
            nj_u4 expected;
            if(step==0)expected=keys[i]?keys[i]:1u;
            else if(step==1)expected=keys[i]<=1?1u:UINT32_MAX;
            else expected=keys[i]==16777216?16777216u:UINT32_MAX;
            if(unpack_key(output+i*4)!=expected)return 0;
        }
    }
    return 1;
}
static int triangles_init(nj_gles2 *s)
{
    triangle_state *t;
    GLint range[2],precision=0;
    const char *extensions;
    size_t i;
    if (!s->geometry) {
        s->geometry=(triangle_state *)calloc(1,sizeof *s->geometry);
        if(!s->geometry)return 0;
    }
    t=s->geometry;
    if(t->ready)return 1;
    if(!current(s))return 0;
    triangles_delete(s);
    if(!clean_errors(s))return 0;
    if(!s->config.desktop_gl) {
        extensions=(const char *)s->gl.GetString(GL_EXTENSIONS);
        if(!triangle_extension(extensions,"GL_EXT_frag_depth")||
           !triangle_extension(extensions,"GL_OES_depth24"))return 0;
        s->gl.GetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
        if(precision<23)return 0;
    }
    t->shaders[0]=triangle_shader(s,GL_VERTEX_SHADER,triangle_seed_vs);
    t->shaders[1]=triangle_shader(s,GL_FRAGMENT_SHADER,triangle_seed_fs);
    t->shaders[2]=triangle_shader(s,GL_VERTEX_SHADER,triangle_draw_vs);
    t->shaders[3]=triangle_shader(s,GL_FRAGMENT_SHADER,triangle_draw_fs);
    for(i=0;i<4;++i)if(!t->shaders[i])goto fail;
    t->seed_program=s->gl.CreateProgram();t->draw_program=s->gl.CreateProgram();
    if(!t->seed_program||!t->draw_program||
       !triangle_link(s,t->seed_program,t->shaders[0],t->shaders[1],1)||
       !triangle_link(s,t->draw_program,t->shaders[2],t->shaders[3],0))goto fail;
#define TRI_UNIFORM(field,program,name) \
    do{t->field=s->gl.GetUniformLocation(t->program,name);if(t->field<0)goto fail;}while(0)
    TRI_UNIFORM(seed_image,seed_program,"image");TRI_UNIFORM(seed_shift,seed_program,"shift");
    TRI_UNIFORM(image,draw_program,"image");TRI_UNIFORM(textured,draw_program,"textured");
    TRI_UNIFORM(wrapping,draw_program,"wrapping");TRI_UNIFORM(function,draw_program,"function");
    TRI_UNIFORM(threshold,draw_program,"threshold");TRI_UNIFORM(fog,draw_program,"fog");
    TRI_UNIFORM(fog_color,draw_program,"fogColor");TRI_UNIFORM(pass,draw_program,"depthPass");
    TRI_UNIFORM(blend,draw_program,"blend");
#undef TRI_UNIFORM
    s->gl.GenRenderbuffers(1,&t->depth);s->gl.GenTextures(1,&t->keys);
    if(!t->depth||!t->keys||!clean_errors(s)||!triangle_depth_gate(s))goto fail;
    for(i=0;i<4;++i){s->gl.DeleteShader(t->shaders[i]);t->shaders[i]=0;}
    if(!clean_errors(s))goto fail;
    {
        static const nj_u1 white[4]={255,255,255,255};
        texture(s,s->source);
        if(!texture_upload(s,&s->source_storage,1,1,GL_RGBA,white))goto fail;
    }
    if(!clean_errors(s))goto fail;
    t->ready=1;return 1;
fail:
    if(current(s)){triangles_delete(s);(void)clean_errors(s);}return 0;
}

static int triangle_same(const nj_gfx_triangle_cmd *a,const nj_gfx_triangle_cmd *b)
{
    return a->texture==b->texture&&a->texture_width==b->texture_width&&
        a->texture_height==b->texture_height&&a->texture_bpp==b->texture_bpp&&
        a->texture_no_alpha==b->texture_no_alpha&&a->wrap_s==b->wrap_s&&a->wrap_t==b->wrap_t&&
        a->filter==b->filter&&a->texture_function==b->texture_function&&a->blend==b->blend&&
        a->alpha_threshold==b->alpha_threshold&&a->alpha_write==b->alpha_write&&
        a->depth_test==b->depth_test&&a->depth_write==b->depth_write&&a->fog==b->fog&&
        a->fog_color==b->fog_color&&a->fog_near==b->fog_near&&a->fog_far==b->fog_far&&
        a->fog_density==b->fog_density;
}
static void triangle_material(nj_gles2 *s,const nj_gfx_triangle_cmd *m,GLuint tex,int depth_pass)
{
    triangle_state *t=s->geometry;
    s->gl.Uniform1i(t->textured,m->texture!=NULL);
    texture(s,m->texture?tex:s->source);
    if(m->texture) {
        s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,m->filter==209?GL_LINEAR:GL_NEAREST);
        s->gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,m->filter==209?GL_LINEAR:GL_NEAREST);
    }
    s->gl.Uniform2f(t->wrapping,m->wrap_s==241?1.0f:0.0f,m->wrap_t==241?1.0f:0.0f);
    s->gl.Uniform2f(t->function,(GLfloat)m->texture_function,
                   (m->texture_no_alpha||m->texture_bpp==3)?1.0f:0.0f);
    s->gl.Uniform1f(t->threshold,(GLfloat)m->alpha_threshold);
    s->gl.Uniform4f(t->fog,(GLfloat)m->fog,m->fog_near,m->fog_far,m->fog_density);
    s->gl.Uniform3f(t->fog_color,(GLfloat)((m->fog_color>>16)&255u)/255.0f,
                  (GLfloat)((m->fog_color>>8)&255u)/255.0f,(GLfloat)(m->fog_color&255u)/255.0f);
    s->gl.Uniform1i(t->blend,m->blend);
    s->gl.Enable(GL_DEPTH_TEST);s->gl.DepthFunc(m->depth_test?GL_LEQUAL:GL_ALWAYS);
    s->gl.DepthMask(!depth_pass&&m->depth_write?GL_TRUE:GL_FALSE);
    s->gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,depth_pass||m->alpha_write?GL_TRUE:GL_FALSE);
    if(depth_pass||m->blend==68)s->gl.Disable(GL_BLEND);
    else {
        s->gl.Enable(GL_BLEND);s->gl.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        if(m->blend==64)s->gl.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        else if(m->blend==65)s->gl.BlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ZERO);
        else if(m->blend==66)s->gl.BlendFuncSeparate(GL_DST_COLOR,GL_ZERO,GL_ONE,GL_ZERO);
        else s->gl.BlendFuncSeparate(GL_DST_COLOR,GL_SRC_COLOR,GL_ONE,GL_ZERO);
    }
}
static void triangle_arrays(nj_gles2 *s,int enable)
{
    triangle_state *t=s->geometry;
    size_t i;
    s->gl.BindBuffer(GL_ARRAY_BUFFER,0);
    for(i=0;i<4;++i) {
        if(enable)s->gl.EnableVertexAttribArray((GLuint)i);
        else s->gl.DisableVertexAttribArray((GLuint)i);
    }
    s->gl.VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(triangle_vertex_gpu),enable?t->vertices->position:NULL);
    s->gl.VertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,sizeof(triangle_vertex_gpu),enable?&t->vertices->depth:NULL);
    s->gl.VertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(triangle_vertex_gpu),enable?t->vertices->uvq:NULL);
    s->gl.VertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(triangle_vertex_gpu),enable?t->vertices->color:NULL);
}
static int triangle_views(nj_gles2 *s,const nj_gfx_triangles *const *packets,
                          nj_u4 npackets,const nj_u4 *offsets,nj_u4 total,
                          triangle_view *views,nj_u4 *indices,nj_u4 *table,
                          nj_u4 table_size,nj_u4 *nviews)
{
    triangle_state *t=s->geometry;
    nj_u4 p,i,n=0;
    int reuse;
    size_t sum=0;
    for(p=0;p<npackets;++p) {
        const nj_gfx_triangles *r=packets[p];
        for(i=0;i<r->count;++i) {
            const nj_gfx_triangle_cmd *m=r->triangles+i;
            nj_u4 flat=offsets[p]+i,slot,j;
            size_t pixels;
            if(!m->texture){indices[flat]=0;continue;}
            if(!dimensions(s,m->texture_width,m->texture_height))return 0;
            slot=(nj_u4)(((uintptr_t)m->texture>>3)^((unsigned)m->texture_width*33u)^((unsigned)m->texture_height*97u)^(unsigned)m->texture_bpp)&(table_size-1u);
            while(table[slot]) {
                triangle_view *v=views+table[slot]-1u;
                if(v->pixels==m->texture&&v->width==m->texture_width&&v->height==m->texture_height&&v->bpp==m->texture_bpp)break;
                slot=(slot+1u)&(table_size-1u);
            }
            if(table[slot])j=table[slot]-1u;
            else {
                pixels=(size_t)m->texture_width*(size_t)m->texture_height;
                if(pixels>s->config.max_pixels-sum)return 0;
                sum+=pixels;j=n++;
                views[j].pixels=m->texture;views[j].width=m->texture_width;
                views[j].height=m->texture_height;views[j].bpp=m->texture_bpp;
                table[slot]=j+1u;
            }
            indices[flat]=j;
        }
    }
    (void)total;
    /* Reuse storage only if the COMPLETE size/format set matches. On any
     * change release all slots first, so failed batches cannot accumulate a
     * sum larger than the pixel budget. Borrowed bytes are uploaded every call. */
    reuse=t->texture_count==n;
    if(reuse) {
        for(i=0;i<n;++i) {
            const triangle_view *v=views+i;
            const texture_storage *st=t->slot_storage+i;
            if(st->width!=v->width||st->height!=v->height||
               st->format!=(v->bpp==4?GL_RGBA:GL_RGB))break;
        }
        reuse=i==n;
    }
    if(!reuse&&t->texture_count) {
        s->gl.DeleteTextures((GLsizei)t->texture_count,t->textures);
        t->texture_count=0;
    }
    if(n>t->texture_capacity) {
        GLuint *names=(GLuint *)realloc(t->textures,(size_t)n*sizeof *names);
        texture_storage *storage;
        if(!names)return 0;
        t->textures=names;
        storage=(texture_storage *)realloc(t->slot_storage,(size_t)n*sizeof *storage);
        if(!storage)return 0;
        t->slot_storage=storage;t->texture_capacity=n;
    }
    if(n>t->texture_count) {
        memset(t->textures,0,(size_t)n*sizeof *t->textures);
        memset(t->slot_storage,0,(size_t)n*sizeof *t->slot_storage);
        t->texture_count=n;
        s->gl.GenTextures((GLsizei)n,t->textures);
    }
    for(i=0;i<n;++i) {
        const triangle_view *v=views+i;
        if(!t->textures[i])return 0;
        texture(s,t->textures[i]);
        if(!texture_upload(s,t->slot_storage+i,v->width,v->height,
                           v->bpp==4?GL_RGBA:GL_RGB,v->pixels))return 0;
    }
    *nviews=n;return clean_errors(s);
}

/* Optional development profiler (build with -DNJ_GLES2_PROFILE=1). It answers
 * where a triangle transaction spends its time: the CPU-side pixel passes,
 * the uploads, the draw, or the two synchronous readbacks. POSIX-only; the
 * default build compiles the whole block out. */
#ifndef NJ_GLES2_PROFILE
#define NJ_GLES2_PROFILE 0
#endif
#if NJ_GLES2_PROFILE
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
static double tri_now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000000.0 / (double)f.QuadPart;
}
#else
/* Dev-only wall clock (POSIX). Avoids pulling POSIX feature macros into this
 * strict-C99 translation unit; never compiled in default builds. */
struct tri_timeval { long tv_sec; long tv_usec; };
extern int gettimeofday(struct tri_timeval *tv, void *tz);
static double tri_now(void)
{
    struct tri_timeval tv;
    gettimeofday(&tv, 0);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}
#endif
static struct tri_prof {
    unsigned long long flushes, pixels, triangles, textures, lists, single;
    double total, val, init, alloc, prep, setup, import_c, up_c, pack_d,
        up_d, seed, views, vbuf, draw, read_c, read_d, commit;
} tri_prof;
static void tri_prof_print(const char *when)
{
    if (!tri_prof.flushes) return;
    fprintf(stderr,
            "PROF %s flushes=%llu px=%llu tri=%llu lists=%llu single=%llu "
            "total=%.1fms val=%.1f init=%.1f alloc=%.1f prep=%.1f "
            "setup=%.1f import_c=%.1f up_c=%.1f pack_d=%.1f up_d=%.1f "
            "seed=%.1f views=%.1f vbuf=%.1f draw=%.1f read_c=%.1f "
            "read_d=%.1f commit=%.1f per_flush_us=%.1f\n",
            when, tri_prof.flushes, tri_prof.pixels, tri_prof.triangles,
            tri_prof.lists, tri_prof.single, tri_prof.total/1000.0,
            tri_prof.val/1000.0, tri_prof.init/1000.0,
            tri_prof.alloc/1000.0, tri_prof.prep/1000.0,
            tri_prof.setup/1000.0,
            tri_prof.import_c/1000.0, tri_prof.up_c/1000.0,
            tri_prof.pack_d/1000.0, tri_prof.up_d/1000.0,
            tri_prof.seed/1000.0, tri_prof.views/1000.0,
            tri_prof.vbuf/1000.0, tri_prof.draw/1000.0,
            tri_prof.read_c/1000.0, tri_prof.read_d/1000.0,
            tri_prof.commit/1000.0,
            tri_prof.flushes ? tri_prof.total/(double)tri_prof.flushes : 0.0);
    fflush(stderr);
}
#endif /* NJ_GLES2_PROFILE */

/* One transaction over an ordered list of packets. The union of the packet
 * ROIs is imported once, seeded once, drawn in packet order, read back once and
 * committed once. No CPU write happens before the commit point. */
static int triangles_run(nj_gles2 *s,const nj_gfx_triangles *const *packets,
                         nj_u4 npackets,nj_u4 *submitted)
{
    triangle_state *t;
    triangle_view *views=NULL;
    nj_u4 *indices=NULL,*table=NULL,*offsets=NULL,table_size=1,nviews=0,i,j,p,flat;
    nj_u4 total=0;
    int x0=0,y0=0,x1=0,y1=0,pass,arrays=0,ok=0,any_write=0,have=0;
    size_t bytes,offset;
#if NJ_GLES2_PROFILE
    double _tp0=tri_now(), _tp=_tp0;
#define TRI_MARK(f) do{double _n=tri_now();tri_prof.f+=_n-_tp;_tp=_n;}while(0)
#endif
    if(!current(s))return decline(s);
    for(p=0;p<npackets;++p) {
        const nj_gfx_triangles *r=packets[p];
        int rx0,ry0,rx1,ry1;
        if(!r||!dimensions(s,r->width,r->height)||r->width>s->max_viewport[0]||
           r->height>s->max_viewport[1])return decline(s);
        if(total>NJ_GFX_TRIANGLES_LIST_MAX_TOTAL-r->count)return decline(s);
        total+=r->count;
        rx0=r->x;ry0=r->y;rx1=r->x+r->width;ry1=r->y+r->height;
        if(!have){x0=rx0;y0=ry0;x1=rx1;y1=ry1;have=1;}
        else {
            if(rx0<x0)x0=rx0;
            if(ry0<y0)y0=ry0;
            if(rx1>x1)x1=rx1;
            if(ry1>y1)y1=ry1;
        }
        for(i=0;i<r->count;++i) {
            const nj_gfx_triangle_cmd *m=r->triangles+i;
            any_write|=m->depth_write;
            for(j=0;j<3;++j) {
                const nj_gfx_triangle_vertex *v=m->vertices+j;
                double px=2.0*((double)v->x-(double)r->x)/(double)r->width-1.0;
                double py=2.0*((double)v->y-(double)r->y)/(double)r->height-1.0;
                if(fabs(v->depth)>1e30||fabs(px)>1e30||fabs(py)>1e30||
                   v->inv_w<1e-20f||v->inv_w>1e20f||
                   fabsf(v->u*v->inv_w)>1e30f||fabsf(v->v*v->inv_w)>1e30f)return decline(s);
            }
        }
    }
#if NJ_GLES2_PROFILE
    TRI_MARK(val);
#endif
    if(!have||x1<=x0||y1<=y0)return decline(s);
    if(!clean_errors(s)||!triangles_init(s))return decline(s);
#if NJ_GLES2_PROFILE
    TRI_MARK(init);
#endif
    t=s->geometry;bytes=(size_t)(x1-x0)*(size_t)(y1-y0)*4u;
    while(table_size<total*2u)table_size*=2u;
    views=(triangle_view *)calloc(total,sizeof *views);
    indices=(nj_u4 *)malloc((size_t)total*sizeof *indices);
    table=(nj_u4 *)calloc(table_size,sizeof *table);
    offsets=(nj_u4 *)malloc((size_t)npackets*sizeof *offsets);
    if(t->vertices_size<(size_t)total*3u*sizeof *t->vertices) {
        size_t size=(size_t)total*3u*sizeof *t->vertices;
        triangle_vertex_gpu *vertices=(triangle_vertex_gpu *)realloc(t->vertices,size);
        if(!vertices)goto finish;
        t->vertices=vertices;t->vertices_size=size;
    }
    if(!views||!indices||!table||!offsets||
       !reserve(&s->upload,&s->upload_size,bytes)||
       (any_write&&!reserve(&s->depth_readback,&s->depth_readback_size,bytes)))goto finish;
#if NJ_GLES2_PROFILE
    TRI_MARK(alloc);
#endif
    if(!prepare(s,x1-x0,y1-y0))goto finish;
#if NJ_GLES2_PROFILE
    TRI_MARK(prep);
#endif
    if(!triangle_storage(s,x1-x0,y1-y0))goto finish;
    for(p=0;p<npackets;++p)offsets[p]=p?offsets[p-1u]+packets[p-1u]->count:0u;
#if NJ_GLES2_PROFILE
    TRI_MARK(setup);
#endif
    /* Import current CPU colour, including transparent alpha. */
    {
        nj_surface *surf=packets[0]->surface;
        for(i=0;i<(nj_u4)(y1-y0);++i)for(j=0;j<(nj_u4)(x1-x0);++j) {
            nj_u4 c=nj_surface_get(surf,x0+(int)j,y0+(int)i);
            nj_u1 *q=s->upload+((size_t)i*(size_t)(x1-x0)+(size_t)j)*4u;
            q[0]=(nj_u1)(c>>16);q[1]=(nj_u1)(c>>8);q[2]=(nj_u1)c;q[3]=(nj_u1)(c>>24);
        }
    }
#if NJ_GLES2_PROFILE
    TRI_MARK(import_c);
#endif
    texture(s,s->target);
    if(!texture_upload(s,&s->target_storage,x1-x0,y1-y0,GL_RGBA,s->upload))goto finish;
#if NJ_GLES2_PROFILE
    TRI_MARK(up_c);
#endif
    {
        const nj_s4 *depth=packets[0]->depth;
        int stride=packets[0]->depth_stride;
        for(i=0;i<(nj_u4)(y1-y0);++i)for(j=0;j<(nj_u4)(x1-x0);++j) {
            nj_u4 key=depth?(nj_u4)depth[(size_t)(y0+(int)i)*(size_t)stride+
                                        (size_t)(x0+(int)j)]:0;
            pack_key(s->upload+((size_t)i*(size_t)(x1-x0)+(size_t)j)*4u,key);
        }
    }
#if NJ_GLES2_PROFILE
    TRI_MARK(pack_d);
#endif
    texture(s,t->keys);
    if(!texture_upload(s,&t->keys_storage,x1-x0,y1-y0,GL_RGBA,s->upload))goto finish;
#if NJ_GLES2_PROFILE
    TRI_MARK(up_d);
#endif
    s->gl.UseProgram(t->seed_program);s->gl.Uniform1i(t->seed_image,0);s->gl.Uniform1f(t->seed_shift,0);
    s->gl.Enable(GL_DEPTH_TEST);s->gl.DepthFunc(GL_ALWAYS);s->gl.DepthMask(GL_TRUE);
    s->gl.ColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);triangle_quad(s);
    if(!clean_errors(s)||!current(s)||
       !triangle_views(s,packets,npackets,offsets,total,views,indices,table,table_size,&nviews))goto finish;
    (void)nviews;
#if NJ_GLES2_PROFILE
    TRI_MARK(seed);
    tri_prof.textures+=(unsigned long long)nviews;
#endif
    for(p=0;p<npackets;++p) {
        const nj_gfx_triangles *r=packets[p];
        for(i=0;i<r->count;++i)for(j=0;j<3;++j) {
            const nj_gfx_triangle_vertex *v=r->triangles[i].vertices+j;
            triangle_vertex_gpu *out=t->vertices+((size_t)(offsets[p]+i)*3u+(size_t)j);
            out->position[0]=(GLfloat)(2.0*((double)v->x-(double)r->x)/(double)r->width-1.0);
            out->position[1]=(GLfloat)(2.0*((double)v->y-(double)r->y)/(double)r->height-1.0);
            out->depth=(GLfloat)v->depth;out->uvq[0]=v->u*v->inv_w;
            out->uvq[1]=v->v*v->inv_w;out->uvq[2]=v->inv_w;
            out->color[0]=(GLfloat)((v->color>>16)&255u)/255.0f;
            out->color[1]=(GLfloat)((v->color>>8)&255u)/255.0f;
            out->color[2]=(GLfloat)(v->color&255u)/255.0f;out->color[3]=(GLfloat)(v->color>>24)/255.0f;
        }
    }
#if NJ_GLES2_PROFILE
    TRI_MARK(views);
#endif
    s->gl.UseProgram(t->draw_program);s->gl.Uniform1i(t->image,0);
    triangle_arrays(s,1);arrays=1;
#if NJ_GLES2_PROFILE
    TRI_MARK(vbuf);
#endif
    for(pass=0;pass<(any_write?2:1);++pass) {
        if(pass) {
            s->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->keys,0);
            if(s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto finish;
        }
        s->gl.Uniform1i(t->pass,pass);
        for(p=0;p<npackets;++p) {
            const nj_gfx_triangles *r=packets[p];
            s->gl.Viewport((GLint)(r->x-x0),(GLint)(r->y-y0),r->width,r->height);
            for(i=0;i<r->count;i=j) {
                const nj_gfx_triangle_cmd *m=r->triangles+i;
                j=i+1u;
                while(j<r->count&&triangle_same(m,r->triangles+j))++j;
                if(pass&&!m->depth_write)continue;
                flat=offsets[p]+i;
                triangle_material(s,m,m->texture?t->textures[indices[flat]]:0,pass);
                s->gl.DrawArrays(GL_TRIANGLES,(GLint)(flat*3u),(GLsizei)((j-i)*3u));
            }
        }
#if NJ_GLES2_PROFILE
        TRI_MARK(draw);
#endif
        s->gl.ReadPixels(0,0,x1-x0,y1-y0,GL_RGBA,GL_UNSIGNED_BYTE,
                         pass?s->depth_readback:s->readback);
        if(!clean_errors(s)||!current(s))goto finish;
#if NJ_GLES2_PROFILE
        if (pass) { TRI_MARK(read_d); } else { TRI_MARK(read_c); }
#endif
    }
    if(any_write)for(offset=0;offset<bytes;offset+=4u)
        if(unpack_key(s->depth_readback+offset)>16777216u)goto finish;
    triangle_arrays(s,0);arrays=0;
    if(!clean_errors(s)||!current(s))goto finish;
    /* No failure-capable operation occurs after the first CPU destination write. */
    {
        nj_surface *surf=packets[0]->surface;
        nj_s4 *depth=packets[0]->depth;
        int stride=packets[0]->depth_stride;
        for(i=0;i<(nj_u4)(y1-y0);++i)for(j=0;j<(nj_u4)(x1-x0);++j) {
            const nj_u1 *q=s->readback+((size_t)i*(size_t)(x1-x0)+(size_t)j)*4u;
            nj_u4 c=((nj_u4)q[3]<<24)|((nj_u4)q[0]<<16)|((nj_u4)q[1]<<8)|q[2];
            nj_surface_set(surf,x0+(int)j,y0+(int)i,c);
            if(any_write&&depth)depth[(size_t)(y0+(int)i)*(size_t)stride+(size_t)(x0+(int)j)]=
                (nj_s4)unpack_key(s->depth_readback+((size_t)i*(size_t)(x1-x0)+(size_t)j)*4u);
        }
    }
    *submitted=total;s->stats.triangles+=total;++s->stats.batches;ok=1;
#if NJ_GLES2_PROFILE
    TRI_MARK(commit);
    ++tri_prof.flushes;
    tri_prof.pixels+=(unsigned long long)(x1-x0)*(unsigned long long)(y1-y0);
    tri_prof.triangles+=total;
    if(npackets==1u)++tri_prof.single;else ++tri_prof.lists;
    tri_prof.total+=tri_now()-_tp0;
    if((tri_prof.flushes%100u)==0u)tri_prof_print("100");
#endif
finish:
    if(arrays&&current(s))triangle_arrays(s,0);
    free(views);free(indices);free(table);free(offsets);
    return ok?NJ_GFX_ACCEL_DONE:decline(s);
}

/* ------------------------------------------------------------------ */
/* Persistent sessions                                                 */
/* ------------------------------------------------------------------ */
/* One session keeps the session rectangle resident: s->framebuffer owns the
 * colour target plus the seeded depth renderbuffer, s->tri_session_fbo owns the
 * matching keys texture with the same depth renderbuffer. Submissions only
 * draw into those attachments; end() reads back and commits once. */

static int triangle_session_seed(nj_gles2 *s,int w,int h)
{
    triangle_state *t=s->geometry;
    nj_surface *surf=s->tri_session_surface;
    nj_s4 *depth=s->tri_session_depth;
    int stride=s->tri_session_stride,x=s->tri_session_x,y=s->tri_session_y;
    nj_u4 i,j;
    for(i=0;i<(nj_u4)h;++i)for(j=0;j<(nj_u4)w;++j) {
        nj_u4 c=nj_surface_get(surf,x+(int)j,y+(int)i);
        nj_u1 *q=s->upload+((size_t)i*(size_t)w+(size_t)j)*4u;
        q[0]=(nj_u1)(c>>16);q[1]=(nj_u1)(c>>8);q[2]=(nj_u1)c;q[3]=(nj_u1)(c>>24);
    }
    texture(s,s->target);
    if(!texture_upload(s,&s->target_storage,w,h,GL_RGBA,s->upload))return 0;
    for(i=0;i<(nj_u4)h;++i)for(j=0;j<(nj_u4)w;++j) {
        nj_u4 key=depth?(nj_u4)depth[(size_t)(y+(int)i)*(size_t)stride+
                                    (size_t)(x+(int)j)]:0;
        pack_key(s->upload+((size_t)i*(size_t)w+(size_t)j)*4u,key);
    }
    texture(s,t->keys);
    if(!texture_upload(s,&t->keys_storage,w,h,GL_RGBA,s->upload))return 0;
    s->gl.UseProgram(t->seed_program);s->gl.Uniform1i(t->seed_image,0);
    s->gl.Uniform1f(t->seed_shift,0.0f);
    s->gl.Enable(GL_DEPTH_TEST);s->gl.DepthFunc(GL_ALWAYS);s->gl.DepthMask(GL_TRUE);
    s->gl.ColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    triangle_quad(s);
    /* Same depth values again, this time packing the ranks into the keys
     * texture. LEQUAL keeps the seeded values and rewrites the same keys. */
    s->gl.DepthFunc(GL_LEQUAL);s->gl.DepthMask(GL_FALSE);
    s->gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->tri_session_fbo);
    triangle_quad(s);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    return clean_errors(s)&&current(s);
}

static int triangle_session_submit(nj_gles2 *s,
                                   const nj_gfx_triangles *const *packets,
                                   nj_u4 npackets,nj_u4 *submitted)
{
    triangle_state *t=s->geometry;
    triangle_view *views=NULL;
    nj_u4 *indices=NULL,*table=NULL,*offsets=NULL,table_size=1u,nviews=0u;
    nj_u4 i,j,p,flat,total=0u,base;
    int pass,arrays=0,ok=0,any_write=0,sx,sy,sw,sh;
    if(!s->tri_session||!current(s))return decline(s);
    sx=s->tri_session_x;sy=s->tri_session_y;
    sw=s->tri_session_w;sh=s->tri_session_h;
    base=s->tri_session_triangles;
    for(p=0;p<npackets;++p) {
        const nj_gfx_triangles *r=packets[p];
        if(!r||r->surface!=s->tri_session_surface||
           r->depth!=s->tri_session_depth||
           r->depth_stride!=s->tri_session_stride)return decline(s);
        if(r->x<sx||r->y<sy||r->x+r->width>sx+sw||r->y+r->height>sy+sh)
            return decline(s);
        if(total>NJ_GFX_TRIANGLES_LIST_MAX_TOTAL-r->count)return decline(s);
        total+=r->count;
        for(i=0;i<r->count;++i)any_write|=r->triangles[i].depth_write;
    }
    if(!total) {
        *submitted=0u;
        return NJ_GFX_ACCEL_DONE;
    }
    if(base>NJ_GFX_TRIANGLES_LIST_MAX_TOTAL-total)return decline(s);
    while(table_size<total*2u)table_size*=2u;
    views=(triangle_view *)calloc(total,sizeof *views);
    indices=(nj_u4 *)malloc((size_t)total*sizeof *indices);
    table=(nj_u4 *)calloc(table_size,sizeof *table);
    offsets=(nj_u4 *)malloc((size_t)npackets*sizeof *offsets);
    if(!views||!indices||!table||!offsets)goto finish;
    if(t->vertices_size<(size_t)(base+total)*3u*sizeof *t->vertices) {
        size_t size=(size_t)(base+total)*3u*sizeof *t->vertices;
        triangle_vertex_gpu *vertices=(triangle_vertex_gpu *)realloc(t->vertices,size);
        if(!vertices)goto finish;
        t->vertices=vertices;t->vertices_size=size;
    }
    for(p=0;p<npackets;++p)offsets[p]=p?offsets[p-1u]+packets[p-1u]->count:0u;
    if(!triangle_views(s,packets,npackets,offsets,total,views,indices,table,
                       table_size,&nviews))goto finish;
    (void)nviews;
    for(p=0;p<npackets;++p) {
        const nj_gfx_triangles *r=packets[p];
        for(i=0;i<r->count;++i)for(j=0;j<3;++j) {
            const nj_gfx_triangle_vertex *v=r->triangles[i].vertices+j;
            triangle_vertex_gpu *out=t->vertices+
                ((size_t)(base+offsets[p]+i)*3u+(size_t)j);
            out->position[0]=(GLfloat)(2.0*((double)v->x-(double)r->x)/(double)r->width-1.0);
            out->position[1]=(GLfloat)(2.0*((double)v->y-(double)r->y)/(double)r->height-1.0);
            out->depth=(GLfloat)v->depth;out->uvq[0]=v->u*v->inv_w;
            out->uvq[1]=v->v*v->inv_w;out->uvq[2]=v->inv_w;
            out->color[0]=(GLfloat)((v->color>>16)&255u)/255.0f;
            out->color[1]=(GLfloat)((v->color>>8)&255u)/255.0f;
            out->color[2]=(GLfloat)(v->color&255u)/255.0f;
            out->color[3]=(GLfloat)(v->color>>24)/255.0f;
        }
    }
    s->gl.UseProgram(t->draw_program);s->gl.Uniform1i(t->image,0);
    triangle_arrays(s,1);arrays=1;
    for(pass=0;pass<(any_write?2:1);++pass) {
        if(pass)s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->tri_session_fbo);
        else s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
        s->gl.Uniform1i(t->pass,pass);
        for(p=0;p<npackets;++p) {
            const nj_gfx_triangles *r=packets[p];
            s->gl.Viewport((GLint)(r->x-sx),(GLint)(r->y-sy),r->width,r->height);
            for(i=0;i<r->count;i=j) {
                const nj_gfx_triangle_cmd *m=r->triangles+i;
                j=i+1u;
                while(j<r->count&&triangle_same(m,r->triangles+j))++j;
                if(pass&&!m->depth_write)continue;
                flat=offsets[p]+i;
                triangle_material(s,m,m->texture?t->textures[indices[flat]]:0,pass);
                s->gl.DrawArrays(GL_TRIANGLES,
                                 (GLint)((base+flat)*3u),
                                 (GLsizei)((j-i)*3u));
            }
        }
        if(!clean_errors(s)||!current(s))goto finish;
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    triangle_arrays(s,0);arrays=0;
    if(!clean_errors(s)||!current(s))goto finish;
    s->tri_session_write|=any_write;
    s->tri_session_triangles=base+total;
    s->stats.triangles+=total;
    ++s->stats.batches;
    *submitted=total;
    ok=1;
finish:
    if(arrays&&current(s))triangle_arrays(s,0);
    free(views);free(indices);free(table);free(offsets);
    return ok?NJ_GFX_ACCEL_DONE:decline(s);
}

int nj_gles2_triangles_begin(void *ud,const nj_gfx_triangles_session *session)
{
    nj_gles2 *s=(nj_gles2 *)ud;
    triangle_state *t;
    size_t bytes;
    if(!s||!session||session->version!=NJ_GFX_TRIANGLES_SESSION_VERSION)
        return decline(s);
    if(!session->surface||!session->surface->pixels||
       session->width<=0||session->height<=0||
       session->x<0||session->y<0||
       session->x+session->width>session->surface->width||
       session->y+session->height>session->surface->height)
        return decline(s);
    if(!current(s)||s->tri_session)return decline(s);
    if(!clean_errors(s)||!triangles_init(s))return decline(s);
    t=s->geometry;
    bytes=(size_t)session->width*(size_t)session->height*4u;
    if(!reserve(&s->upload,&s->upload_size,bytes)||
       !reserve(&s->readback,&s->readback_size,bytes)||
       !reserve(&s->depth_readback,&s->depth_readback_size,bytes))
        return decline(s);
    if(!prepare(s,session->width,session->height))return decline(s);
    if(!triangle_storage(s,session->width,session->height))return decline(s);
    if(!s->tri_session_fbo) {
        s->gl.GenFramebuffers(1,&s->tri_session_fbo);
        if(!s->tri_session_fbo)return decline(s);
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->tri_session_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->keys,0);
    s->gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,t->depth);
    if(s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
       !clean_errors(s)) {
        s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
        return decline(s);
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    s->tri_session_surface=session->surface;
    s->tri_session_depth=session->depth;
    s->tri_session_stride=session->depth_stride;
    s->tri_session_x=session->x;s->tri_session_y=session->y;
    s->tri_session_w=session->width;s->tri_session_h=session->height;
    s->tri_session_write=0;s->tri_session_triangles=0u;
    s->tri_session=1;
    if(!triangle_session_seed(s,session->width,session->height)) {
        s->tri_session=0;
        s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
        (void)clean_errors(s);
        return decline(s);
    }
    return NJ_GFX_ACCEL_DONE;
}

int nj_gles2_triangles_end(void *ud,nj_u4 *submitted)
{
    nj_gles2 *s=(nj_gles2 *)ud;
    nj_surface *surf;
    nj_s4 *depth;
    int w,h,x,y,stride,i,j;
    size_t bytes;
    if(!s||!s->tri_session)return decline(s);
    surf=s->tri_session_surface;depth=s->tri_session_depth;
    stride=s->tri_session_stride;x=s->tri_session_x;y=s->tri_session_y;
    w=s->tri_session_w;h=s->tri_session_h;
    if(!current(s)) { s->tri_session=0;return decline(s); }
    bytes=(size_t)w*(size_t)h*4u;
    if(!reserve(&s->readback,&s->readback_size,bytes)||
       (s->tri_session_write&&!reserve(&s->depth_readback,&s->depth_readback_size,bytes))) {
        s->tri_session=0;
        return decline(s);
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    s->gl.ReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,s->readback);
    if(s->tri_session_write) {
        s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->tri_session_fbo);
        s->gl.ReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,s->depth_readback);
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
    if(!clean_errors(s)||!current(s)) { s->tri_session=0;return decline(s); }
    if(s->tri_session_write) {
        for(i=0;i<w*h;++i)
            if(unpack_key(s->depth_readback+(size_t)i*4u)>16777216u) {
                s->tri_session=0;
                return decline(s);
            }
    }
    /* No failure-capable operation occurs after the first CPU destination write. */
    for(i=0;i<h;++i)for(j=0;j<w;++j) {
        const nj_u1 *q=s->readback+((size_t)i*(size_t)w+(size_t)j)*4u;
        nj_u4 c=((nj_u4)q[3]<<24)|((nj_u4)q[0]<<16)|((nj_u4)q[1]<<8)|q[2];
        nj_surface_set(surf,x+j,y+i,c);
        if(s->tri_session_write&&depth)
            depth[(size_t)(y+i)*(size_t)stride+(size_t)(x+j)]=
                (nj_s4)unpack_key(s->depth_readback+
                                  ((size_t)i*(size_t)w+(size_t)j)*4u);
    }
    *submitted=s->tri_session_triangles;
    s->tri_session=0;
    return NJ_GFX_ACCEL_DONE;
}

void nj_gles2_triangles_abort(void *ud)
{
    nj_gles2 *s=(nj_gles2 *)ud;
    if(!s)return;
    if(s->tri_session&&current(s)) {
        s->gl.BindFramebuffer(GL_FRAMEBUFFER,s->framebuffer);
        (void)clean_errors(s);
    }
    s->tri_session=0;
}

int nj_gles2_triangles(void *ud,const nj_gfx_triangles *r,nj_u4 *submitted)
{
    nj_gles2 *s=(nj_gles2 *)ud;
    const nj_gfx_triangles *packets[1];
    if(!current(s)||!nj_gfx_triangles_validate(r,submitted))return decline(s);
    packets[0]=r;
    if(s->tri_session)return triangle_session_submit(s,packets,1u,submitted);
    return triangles_run(s,packets,1u,submitted);
}
int nj_gles2_triangles_list(void *ud,const nj_gfx_triangles_list *list,
                            nj_u4 *submitted)
{
    nj_gles2 *s=(nj_gles2 *)ud;
    if(!current(s)||!nj_gfx_triangles_list_validate(list,submitted))return decline(s);
    if(s->tri_session)return triangle_session_submit(s,list->packets,list->count,submitted);
    return triangles_run(s,list->packets,list->count,submitted);
}
