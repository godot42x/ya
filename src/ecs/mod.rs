use std::any::TypeId;

use bytemuck::Pod;

/**
 * A simple ECS (Entity-Component-System) implementation.
 * Entities are represented by unique IDs, and components are stored in a registry.
 *  Same type's components are stored in contiguous memory for cache efficiency.
 * Each component type has its own storage, and entities can have multiple components.
 *
 */

struct Registry {
    components: std::collections::HashMap<TypeId, ComponentStorage>,
    id: u32,
}

pub trait Component {
    fn create() -> Self
    where
        Self: Sized;
}

impl Registry {
    pub fn new() -> Self {
        Self {
            components: std::collections::HashMap::new(),
            id: 0,
        }
    }
    pub fn create(&mut self) -> u32 {
        self.id += 1;
        self.id
    }

    // pub fn add_component<T: 'static + Pod + Component>(&mut self, entity: u32) -> &mut T {
    //     let type_id = TypeId::of::<T>();
    //     let component_size = std::mem::size_of::<T>();

    //     let &mut row = self
    //         .components
    //         .entry(type_id)
    //         .or_insert_with(|| ComponentStorage {
    //             data: Vec::new(),
    //             component_size,
    //             component_type: type_id,
    //         });

    //     // Ensure the data vector is large enough
    //     let required_size = (entity as usize + 1) * component_size;
    //     if row.data.len() < required_size {
    //         row.data.resize(required_size, 0);
    //     }

    //     // Store the component data at the correct offset
    //     let offset = entity as usize * component_size;
    //     let comp = T::create();
    //     let bytes =
    //         unsafe { std::slice::from_raw_parts(&comp as *const T as *const u8, component_size) };

    //     row.data[offset..offset + component_size].copy_from_slice(bytes);
    // }

    pub fn get_component<T: 'static + Pod>(&mut self, entity: u32) -> Option<&mut T> {
        let type_id = TypeId::of::<T>();
        let row = self.components.get(&type_id)?;
        row.get_component::<T>(entity)
    }
}

struct ComponentStorage {
    data: Vec<u8>,
    component_size: usize,
    component_type: TypeId,
}

impl ComponentStorage {
    pub fn get_component<T: 'static + Copy>(&self, entity: u32) -> Option<&mut T> {
        if self.component_type != TypeId::of::<T>() {
            return None;
        }
        let offset = entity as usize * self.component_size;
        if offset + self.component_size > self.data.len() {
            return None;
        }
        let bytes = &self.data[offset..offset + self.component_size];
        Some(unsafe { &mut *(bytes.as_ptr() as *mut T) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[repr(C)]
    struct TestComponent {
        x: f32,
        y: f32,
    }

    #[test]
    fn test_ecs() {
        // let mut registry = Registry::new();
        // let entity = registry.create();
        // registry.add_component(entity, 42);
        // let component = registry.get_component::<i32>(entity);
        // assert_eq!(component, Some(&mut 42));

        // let entity2 = registry.create();
        // registry.add_component(entity2, TestComponent { x: 1.0, y: 2.0 });
    }
}
