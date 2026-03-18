use crate::app::RenderContext;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RenderTechnique {
    Forward,
    Deferred,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) enum PassNodeKind {
    Overlay2d,
    Opaque3d,
    // Deferred passes
    GBuffer,
    Lighting,
}

#[derive(Clone, Debug)]
pub(super) struct FramePlan {
    pub technique: RenderTechnique,
    pub passes: Vec<PassNodeKind>,
}

impl FramePlan {
    pub(super) fn from_context(_render_ctx: &RenderContext, preferred: RenderTechnique) -> Self {
        match preferred {
            RenderTechnique::Forward => Self {
                technique: RenderTechnique::Forward,
                passes: vec![PassNodeKind::Overlay2d, PassNodeKind::Opaque3d],
            },
            RenderTechnique::Deferred => Self {
                technique: RenderTechnique::Deferred,
                // GBuffer writes geometry; Lighting reads it; Overlay2d draws UI on top
                passes: vec![
                    PassNodeKind::GBuffer,
                    PassNodeKind::Lighting,
                    PassNodeKind::Overlay2d,
                ],
            },
        }
    }
}
