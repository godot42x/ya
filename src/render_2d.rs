use std::collections::HashMap;

use bytemuck::bytes_of;

#[derive(Hash, Eq, PartialEq)]
pub enum RHIDrawType {
    Quad,
    Triangle,
    Line,
}

pub struct RHICommand {
    vertices: Vec<f32>,
    indices: Vec<u16>,
}

pub struct RHICommandList {
    pub commands: HashMap<RHIDrawType, Vec<RHICommand>>,
    pub vertex_buffer: wgpu::Buffer,
    pub index_buffer: wgpu::Buffer,
}

fn draw_quad(command_list: &mut RHICommandList, pos: [f32; 2], size: [f32; 2], color: [f32; 4]) {
    command_list
        .commands
        .entry(RHIDrawType::Quad)
        .or_default()
        .push(RHICommand {
            vertices: vec![
                // bottom-left
                pos[0],
                pos[1],
                color[0],
                color[1],
                color[2],
                color[3],
                // bottom-right
                pos[0] + size[0],
                pos[1],
                color[0],
                color[1],
                color[2],
                color[3],
                // top-right
                pos[0] + size[0],
                pos[1] + size[1],
                color[0],
                color[1],
                color[2],
                color[3],
                pos[0],
                // top-left
                pos[1] + size[1],
                color[0],
                color[1],
                color[2],
                color[3],
            ],
            indices: vec![0, 1, 2, 2, 3, 0],
        });
}

fn batch(
    device: &mut wgpu::Device,
    rp: &mut wgpu::RenderPass,
    pl: &mut wgpu::RenderPipeline,
    rhi_cmds: &mut RHICommandList,
) {
    if let Some(quad_cmds) = rhi_cmds.commands.get(&RHIDrawType::Quad) {
        let mut v_offset = 0;
        let mut i_offset = 0;

        // each cmd a draw call
        for cmd in quad_cmds {
            {
                let new_vertex_size =
                    rhi_cmds.vertex_buffer.size().clone() + (cmd.vertices.len() * 4) as u64;
                let new_index_size =
                    rhi_cmds.index_buffer.size().clone() + (cmd.indices.len() * 2) as u64;
                try_extend(
                    &device,
                    &mut rhi_cmds.vertex_buffer,
                    &mut rhi_cmds.index_buffer,
                    new_vertex_size,
                    new_index_size,
                );
            }

            rp.set_pipeline(pl);
            let bounds = v_offset..v_offset + cmd.vertices.len() as u64;
            let mut vbv = rhi_cmds.vertex_buffer.get_mapped_range_mut(bounds);
            vbv.copy_from_slice(bytemuck::cast_slice(&cmd.vertices));
            v_offset += cmd.vertices.len() as u64;
            rhi_cmds.vertex_buffer.unmap();

            let ibounds = i_offset..i_offset + cmd.indices.len() as u64;
            let mut ibv = rhi_cmds.index_buffer.get_mapped_range_mut(ibounds);
            ibv.copy_from_slice(bytemuck::cast_slice(&cmd.indices));
            i_offset += cmd.indices.len() as u64;
            rhi_cmds.index_buffer.unmap();
        }
    }
}

fn try_extend(
    device: &wgpu::Device,
    vb: &mut wgpu::Buffer,
    ib: &mut wgpu::Buffer,
    vertex_size: u64,
    index_size: u64,
) {
    if vb.size() < vertex_size {
        *vb = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("vertex buffer"),
            size: vertex_size,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: true,
        });
    }
    if ib.size() < index_size {
        *ib = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("index buffer"),
            size: index_size,
            usage: wgpu::BufferUsages::INDEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: true,
        });
    }
}
