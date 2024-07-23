#ifndef RENDER_PASS_H
#define RENDER_PASS_H

class RenderDelegate;

#include <Common.h>

class RenderPass final : public HdRenderPass
{
public:
    RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const &collection, RenderDelegate* pRenderDelegate);
    virtual ~RenderPass();

protected:

    void _Execute(HdRenderPassStateSharedPtr const& pRenderPassState, TfTokenVector const &renderTags) override;

private:
    RenderDelegate* m_Owner;

    Image m_ColorAttachment;
    Image m_DepthAttachment;
};

#endif