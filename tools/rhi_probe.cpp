// rhi_probe.cpp —— QRhi 翻转层定案探针（已知图案法）。
// 阶段1：上传"上半白/下半黑"→ 直接回读（测上传/回读行序）。
// 阶段2：blurH 式 1:1 渲染通道（纹理采样→渲染目标）→ 回读（测渲染写入行序）。
#include <QGuiApplication>
#include <QImage>
#include <QFile>
#include <rhi/qrhi.h>
#include <cstdio>

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        printf("no shader: %s\n", qPrintable(path));
        return QShader();
    }
    return QShader::fromSerialized(f.readAll());
}

static void dump(const QImage &img, const char *tag)
{
    int firstWhite = -1, lastWhite = -1;
    for (int y = 0; y < img.height(); ++y) {
        const uchar *line = img.constScanLine(y);
        bool white = false;
        for (int x = 0; x < img.width(); ++x)
            if (line[x * 4] > 200 && line[x * 4 + 1] > 200 && line[x * 4 + 2] > 200) {
                white = true;
                break;
            }
        if (white) {
            if (firstWhite < 0) firstWhite = y;
            lastWhite = y;
        }
    }
    printf("%s: %dx%d white rows %d..%d (h=%d)\n", tag, img.width(), img.height(),
           firstWhite, lastWhite, img.height());
    img.save(QString::fromLatin1("/tmp/pattern_%1.png").arg(QLatin1String(tag)));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QRhi *r = QRhi::create(QRhi::Metal, nullptr);
    if (!r) {
        printf("no rhi\n");
        return 1;
    }
    printf("backend: %s\n", r->backendName());
    const QSize sz(64, 32);
    QImage pat(sz, QImage::Format_RGBA8888);
    for (int y = 0; y < sz.height(); ++y) {
        QRgb *px = reinterpret_cast<QRgb *>(pat.scanLine(y));
        const bool white = y < sz.height() / 2;
        for (int x = 0; x < sz.width(); ++x)
            px[x] = white ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
    }

    QRhiTexture *texPat = r->newTexture(QRhiTexture::RGBA8, sz, 1,
                                        QRhiTexture::UsedAsTransferSource);
    texPat->create();
    QRhiTexture *texOut = r->newTexture(QRhiTexture::RGBA8, sz, 1,
                                        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    texOut->create();
    QRhiTextureRenderTarget *rt = r->newTextureRenderTarget({ texOut });
    rt->setRenderPassDescriptor(rt->newCompatibleRenderPassDescriptor());

    // 阶段1：上传 → 回读（无渲染）
    {
        QRhiResourceUpdateBatch *u = r->nextResourceUpdateBatch();
        u->uploadTexture(texPat, pat);
        QRhiCommandBuffer *cb = nullptr;
        if (r->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return 1;
        cb->beginPass(rt, QColor(0, 0, 0), QRhiDepthStencilClearValue(), u);
        cb->endPass();
        QRhiReadbackResult *rb = new QRhiReadbackResult;
        bool done = false;
        rb->completed = [&done, rb] { done = true; delete rb; };
        QRhiResourceUpdateBatch *ru = r->nextResourceUpdateBatch();
        ru->readBackTexture(texPat, rb);
        cb->resourceUpdate(ru);
        r->endOffscreenFrame();
        r->finish();
        printf("stage1 readback done=%d (data=%s)\n", done, rb->data.isEmpty() ? "EMPTY" : "OK");
    }

    // 阶段2：blurH 式 1:1 渲染（texPat → texOut）
    {
        QRhiSampler *smp = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        smp->create();
        QRhiBuffer *ubuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 192);
        ubuf->create();
        QRhiShaderResourceBindings *srb = r->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                        | QRhiShaderResourceBinding::FragmentStage, ubuf),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      texPat, smp),
        });
        srb->create();
        QRhiGraphicsPipeline *ps = r->newGraphicsPipeline();
        ps->setShaderStages({
            { QRhiShaderStage::Vertex, loadShader(QStringLiteral("build/.qsb/shaders/crt.vert.qsb")) },
            { QRhiShaderStage::Fragment, loadShader(QStringLiteral("build/.qsb/shaders/blurH.frag.qsb")) },
        });
        QRhiVertexInputLayout vin;
        vin.setBindings({});
        ps->setVertexInputLayout(vin);
        ps->setSampleCount(1);
        ps->setTopology(QRhiGraphicsPipeline::Triangles);
        ps->setShaderResourceBindings(srb);
        ps->setRenderPassDescriptor(rt->renderPassDescriptor());
        if (!ps->create()) {
            printf("ps create fail\n");
            return 1;
        }
        QRhiCommandBuffer *cb = nullptr;
        if (r->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return 1;
        cb->beginPass(rt, QColor(0, 0, 0), QRhiDepthStencilClearValue(), nullptr);
        cb->setGraphicsPipeline(ps);
        cb->setShaderResources(srb);
        cb->setViewport(QRhiViewport(0, 0, float(sz.width()), float(sz.height())));
        cb->draw(3);
        cb->endPass();
        QRhiReadbackResult *rb = new QRhiReadbackResult;
        QImage out;
        rb->completed = [&out, rb] {
            QImage img(64, 32, QImage::Format_RGBA8888);
            if (!rb->data.isEmpty())
                memcpy(img.bits(), rb->data.constData(), size_t(img.sizeInBytes()));
            out = img;
            delete rb;
        };
        QRhiResourceUpdateBatch *ru = r->nextResourceUpdateBatch();
        ru->readBackTexture(texOut, rb);
        cb->resourceUpdate(ru);
        r->endOffscreenFrame();
        r->finish();
        dump(out, "stage2");
        QImage outM = out.mirrored(false, true);
        dump(outM, "stage2-mirrored");
    }
    printf("done\n");
    return 0;
}
