#pragma once
#include "Base/Esoterica.h"

//-------------------------------------------------------------------------

namespace EE
{
    class TaskSystem;
    namespace Resource { class ResourceSystem; }
    namespace TypeSystem { class TypeRegistry; }
}

//-------------------------------------------------------------------------

namespace EE::EntityModel
{
    struct LoadingContext
    {
        LoadingContext() = default;

        LoadingContext( TaskSystem* pTaskSystem, TypeSystem::TypeRegistry const* pTypeRegistry, Resource::ResourceSystem* pResourceSystem )
            : m_pTaskSystem( pTaskSystem )
            , m_pTypeRegistry( pTypeRegistry )
            , m_pResourceSystem( pResourceSystem )
        {
            EE_ASSERT( m_pTypeRegistry != nullptr && m_pResourceSystem != nullptr );
        }

        inline bool IsValid() const
        {
            return m_pTypeRegistry != nullptr && m_pResourceSystem != nullptr;
        }

    public:

        TaskSystem*                                                     m_pTaskSystem = nullptr;
        TypeSystem::TypeRegistry const*                                 m_pTypeRegistry = nullptr;
        Resource::ResourceSystem*                                       m_pResourceSystem = nullptr;
    };
}