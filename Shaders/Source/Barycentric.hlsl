#ifndef BARYCENTRIC_H
#define BARYCENTRIC_H

// Barycentric Coordinate + Interp Util
// Ref: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// ---------------------------------

namespace Barycentrics
{
    struct Data
    {
        float3 m_lambda;
        float3 m_ddx;
        float3 m_ddy;
    };

    Data Compute(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
    {
        Data ret = (Data)0;

        float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

        float2 ndc0 = pt0.xy * invW.x;
        float2 ndc1 = pt1.xy * invW.y;
        float2 ndc2 = pt2.xy * invW.z;

        float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
        ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
        ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
        float ddxSum = dot(ret.m_ddx, float3(1,1,1));
        float ddySum = dot(ret.m_ddy, float3(1,1,1));

        float2 deltaVec = pixelNdc - ndc0;
        float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
        float interpW = rcp(interpInvW);

        ret.m_lambda.x = interpW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
        ret.m_lambda.y = interpW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
        ret.m_lambda.z = interpW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

        ret.m_ddx *= (2.0f/winSize.x);
        ret.m_ddy *= (2.0f/winSize.y);
        ddxSum    *= (2.0f/winSize.x);
        ddySum    *= (2.0f/winSize.y);

        ret.m_ddy *= -1.0f;
        ddySum    *= -1.0f;

        float interpW_ddx = 1.0f / (interpInvW + ddxSum);
        float interpW_ddy = 1.0f / (interpInvW + ddySum);

        ret.m_ddx = interpW_ddx*(ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
        ret.m_ddy = interpW_ddy*(ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;  

        return ret;
    }
}

#endif