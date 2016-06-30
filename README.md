# cppinvert

## Motivations and Implementation

This inversion of control container serves two purposes: Acting as an object bag and acting as an object factory.

The first is just a generic way (and more importantly a generic interface) for holding objects and passing them around so that the dependencies are light if you add a new object to the bag, and the client of that container retrieves the bag, those are the only classes that change, and both changes can potentially be in the cpp, so potentially only recompiling two object files. Where if it was a header change, it could trigger a lot more recompilation and linking.

But, it also is used as a factory. So if the container isn't holding a particular object, you can tell it how to create the object. The nice thing is that the client doesn't know if the IOC container is creating an object or just holding an object for you. So it keeps the retriever generic as to not be tied to that information or to have to worry too much about object lifetimes (in most cases).

One of the common IOC container anti-patterns is storing everything in a single container. It basically then degrades to being like a global. So, one of the things I did in this one is automatically teach it how to create new subcontainers. I think making that easy to do is important so that when you pass along a container, you retrieve a subcontainer for members, so the classes aren't referencing information that they shouldn't have or shouldn't pertain to them. (edited)

But it's also important that the child containers still preserve all the information about how to create objects. Currently, I copied the factory information to the child. But, in retrospect, it might be better to make a parent pointer so the child can ask the parent if it knows how to create an object.

Imagine the strategy pattern:

```
class Bird
{
public:
       Bird(IQuackPolicy* quackPolicy, IFlyPolicy* flyPolicy, etc...);
};
```

If you were doing this in pure C++, it might look like this (Let's ignore memory management for the moment):

```
Bird duck(new DuckQuackPolicy(), new DuckFlyPolicy());
```


However, it could also look like this:

```
DuckQuackPolicy quackPolicy;
     DuckFlyPolicy flyPolicy;
     ... These are used somewhere ...
     Bird duck(&quackPolicy, &flyPolicy);
```

So you see that there's two use cases for how Bird is constructed, one where the objects are created at construction and one where the objects are stored and later they are passed in.

That's basically what the IOC container is emulating.

If you split the containers, then you would add knowledge to the client of whether it's a creation operation or a retrieval operation, and from the client perspective it doesn't matter. That's the dependency injection part.

So with the IOC container, you'd do this:

```
    IocContainer iocContainer;
    iocContainer.bindFactory<QuackPolicy>([]() { return new DuckQuackPolicy(); });
    iocContainer.bindFactory<FlyPolicy>([]() { return new DuckFlyPolicy(); });
    ...
    // and then the duck can retrieve and not be the wiser that it's a creation operation    
    Bird duck(iocContainer);
```

or...

```
    IocContainer iocContainer;
    iocContainer.bindInstance<QuackPolicy>(&quackPolicy);
    iocContainer.bindInstance<FlyPolicy>(&flyPolicy);
    ...
    // and then the duck can retrieve and not be the wiser that it's a retrieval operation
    Bird duck(iocContainer);
```

Let's imagine a case like this: You have a graphics engine, you don't know the underlying implementation at all, and you want to create 8 viewports (I've had such an application).

So you might have something like this:

```
    Window::setup()
    {
         size_t numHorizViewports = m_iocContainer.get<size_t>("numHorizViewports");
         size_t numVertViewports = m_iocContainer.get<size_t>("numVertViewports");
         ++count;
         for (size_t x = 0; x < numHorizViewports; ++x)
         {
              for (size_t y = 0; y < numVertViewports; ++y)
              {
                      string name(str(format("viewport%1%") % count));
                      auto& viewport = iocContainer.getRef<IViewport>("viewport");
                      viewport.arrange(... calculate position ...);
              }
         }
    }
```

And meanwhile, at a higher level, you do something like this:

```
iocContainer.bindInstance<size_t>("numHorizViewports", 2);
    iocContainer.bindInstance<size_t>("numVertViewports", 4);
    iocContainer.bindFactory<IViewport>([]() { return new Direct3DViewport(); });
```

What this gets you is not tying that knowledge into the Window class. Maybe it doesn't need to know the type of viewport. But you tell it how to create it. But the main point of all this is resistance to change. When you make a change in one place, you want to reduce the ripple effect, Which is what this achieves.

Creation via factory mechanism:

Instead of:
```
iocContainer->get<IViewport>();
```
Can do something like:
```
iocContainer->create<IViewport>(width, height, pos, etc...);
```

## Future Todos
- Ability to configure IOC container from configuration file
- Injection
