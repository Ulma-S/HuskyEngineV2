// Rigidbody2D.h の実装(PHY-2b: 速度と力)。
#include "engine/Rigidbody2D.h"

namespace husky::engine
{
    void Rigidbody2D::SetLinearVelocity(const core::Vector2& velocity)
    {
        m_pendingVelocity = velocity; // 体を置き直したとき・体が無い間に読まれたときのためにも覚える
        if (HasBody())
        {
            m_physicsWorld->SetLinearVelocity(m_physicsBody, velocity);
        }
    }

    core::Vector2 Rigidbody2D::GetLinearVelocity() const
    {
        return HasBody() ? m_physicsWorld->GetLinearVelocity(m_physicsBody) : m_pendingVelocity;
    }

    void Rigidbody2D::SetAngularVelocity(float degreesPerSecond)
    {
        m_pendingAngularVelocity = degreesPerSecond;
        if (HasBody())
        {
            m_physicsWorld->SetAngularVelocity(m_physicsBody, degreesPerSecond);
        }
    }

    float Rigidbody2D::GetAngularVelocity() const
    {
        return HasBody() ? m_physicsWorld->GetAngularVelocity(m_physicsBody) : m_pendingAngularVelocity;
    }

    void Rigidbody2D::ApplyForce(const core::Vector2& force)
    {
        if (HasBody())
        {
            m_physicsWorld->ApplyForce(m_physicsBody, force);
        }
    }

    void Rigidbody2D::ApplyImpulse(const core::Vector2& impulse)
    {
        if (HasBody())
        {
            m_physicsWorld->ApplyImpulse(m_physicsBody, impulse);
        }
    }

    float Rigidbody2D::GetMass() const
    {
        return HasBody() ? m_physicsWorld->GetMass(m_physicsBody) : 0.0f;
    }

    void Rigidbody2D::ConnectBody(PhysicsWorld2D* world, PhysicsBodyHandle2D body)
    {
        const bool first = !HasBody();
        m_physicsWorld = world;
        m_physicsBody = body;
        if (first && HasBody())
        {
            // 体ができる前に頼まれた速度を入れる(置き直しのときは、PhysicsScene2D が前の体の速さを引き継いでいる)
            m_physicsWorld->SetLinearVelocity(m_physicsBody, m_pendingVelocity);
            m_physicsWorld->SetAngularVelocity(m_physicsBody, m_pendingAngularVelocity);
        }
    }
}
