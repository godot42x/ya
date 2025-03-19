
#pragma once

#include <string>
#include <vector>

struct Node;

struct Property
{
    std::string name;
};

struct Node
{
    std::vector<Node> children;
    Property          properties;
};


enum TagKind
{
    Text
};

struct Tag
{
    Tag(TagKind kind) {}
    Tag(std::string_view anyKind) {}
};


void a()
{
    /**
    <div>
        <text> hello world </>
        <button onClick="alert('hello')"/>
    </>

    Tag<div>{
        .name = "div"
        .size = {
            .width = 100,
            .height = 100
            .width = "100"
        },
        
    }[
        Tag<text> hello world{},
        Tag<button>{}.onClick("alert('hello')")
    ]
    */
}
