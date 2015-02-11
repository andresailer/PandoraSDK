/**
 *  @file   PandoraSDK/src/Objects/Cluster.cc
 * 
 *  @brief  Implementation of the cluster class.
 * 
 *  $Log: $
 */

#include "Managers/PluginManager.h"

#include "Objects/CaloHit.h"
#include "Objects/Cluster.h"
#include "Objects/Track.h"

#include "Pandora/Pandora.h"

#include "Plugins/EnergyCorrectionsPlugin.h"
#include "Plugins/ParticleIdPlugin.h"
#include "Plugins/ShowerProfilePlugin.h"

namespace pandora
{

Cluster::Cluster(const PandoraContentApi::Cluster::Parameters &parameters) :
    m_nCaloHits(0),
    m_nPossibleMipHits(0),
    m_electromagneticEnergy(0),
    m_hadronicEnergy(0),
    m_isolatedElectromagneticEnergy(0),
    m_isolatedHadronicEnergy(0),
    m_particleId(UNKNOWN_PARTICLE_TYPE),
    m_pTrackSeed(parameters.m_pTrack.IsInitialized() ? parameters.m_pTrack.Get() : NULL),
    m_initialDirection(0.f, 0.f, 0.f),
    m_isDirectionUpToDate(false),
    m_isFitUpToDate(false),
    m_isAvailable(true)
{
    if (parameters.m_caloHitList.empty() && parameters.m_isolatedCaloHitList.empty() && !parameters.m_pTrack.IsInitialized())
        throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

    if (parameters.m_pTrack.IsInitialized())
    {
        m_initialDirection = parameters.m_pTrack.Get()->GetTrackStateAtCalorimeter().GetMomentum().GetUnitVector();
        m_isDirectionUpToDate = true;
    }

    for (CaloHitList::const_iterator iter = parameters.m_caloHitList.begin(), iterEnd = parameters.m_caloHitList.end(); iter != iterEnd; ++iter)
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->AddCaloHit(*iter));
    }

    for (CaloHitList::const_iterator iter = parameters.m_isolatedCaloHitList.begin(), iterEnd = parameters.m_isolatedCaloHitList.end(); iter != iterEnd; ++iter)
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->AddIsolatedCaloHit(*iter));
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::AlterMetadata(const PandoraContentApi::Cluster::Metadata &metadata)
{
    if (metadata.m_particleId.IsInitialized())
    {
        m_isPhotonFast.Reset();
        m_particleId = metadata.m_particleId.Get();
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::AddCaloHit(const CaloHit *const pCaloHit)
{
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, m_orderedCaloHitList.Add(pCaloHit));

    this->ResetOutdatedProperties();

    m_nCaloHits++;

    if (pCaloHit->IsPossibleMip())
        m_nPossibleMipHits++;

    const float x(pCaloHit->GetPositionVector().GetX());
    const float y(pCaloHit->GetPositionVector().GetY());
    const float z(pCaloHit->GetPositionVector().GetZ());

    m_electromagneticEnergy += pCaloHit->GetElectromagneticEnergy();
    m_hadronicEnergy += pCaloHit->GetHadronicEnergy();

    const unsigned int pseudoLayer(pCaloHit->GetPseudoLayer());
    OrderedCaloHitList::const_iterator iter = m_orderedCaloHitList.find(pseudoLayer);

    if ((m_orderedCaloHitList.end() != iter) && (iter->second->size() > 1))
    {
        m_sumXByPseudoLayer[pseudoLayer] += x;
        m_sumYByPseudoLayer[pseudoLayer] += y;
        m_sumZByPseudoLayer[pseudoLayer] += z;
    }
    else
    {
        m_sumXByPseudoLayer[pseudoLayer] = x;
        m_sumYByPseudoLayer[pseudoLayer] = y;
        m_sumZByPseudoLayer[pseudoLayer] = z;
    }

    if (!m_innerPseudoLayer.IsInitialized() || (pseudoLayer < m_innerPseudoLayer.Get()))
        m_innerPseudoLayer = pseudoLayer;

    if (!m_outerPseudoLayer.IsInitialized() || (pseudoLayer > m_outerPseudoLayer.Get()))
        m_outerPseudoLayer = pseudoLayer;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::RemoveCaloHit(const CaloHit *const pCaloHit)
{
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, m_orderedCaloHitList.Remove(pCaloHit));

    if (m_orderedCaloHitList.empty())
        return this->ResetProperties();

    this->ResetOutdatedProperties();

    m_nCaloHits--;

    if (pCaloHit->IsPossibleMip())
        m_nPossibleMipHits--;

    const float x(pCaloHit->GetPositionVector().GetX());
    const float y(pCaloHit->GetPositionVector().GetY());
    const float z(pCaloHit->GetPositionVector().GetZ());

    m_electromagneticEnergy -= pCaloHit->GetElectromagneticEnergy();
    m_hadronicEnergy -= pCaloHit->GetHadronicEnergy();

    const unsigned int pseudoLayer(pCaloHit->GetPseudoLayer());

    if (m_orderedCaloHitList.end() != m_orderedCaloHitList.find(pseudoLayer))
    {
        m_sumXByPseudoLayer[pseudoLayer] -= x;
        m_sumYByPseudoLayer[pseudoLayer] -= y;
        m_sumZByPseudoLayer[pseudoLayer] -= z;
    }
    else
    {
        m_sumXByPseudoLayer.erase(pseudoLayer);
        m_sumYByPseudoLayer.erase(pseudoLayer);
        m_sumZByPseudoLayer.erase(pseudoLayer);
    }

    if (pseudoLayer <= m_innerPseudoLayer.Get())
        m_innerPseudoLayer = m_orderedCaloHitList.begin()->first;

    if (pseudoLayer >= m_outerPseudoLayer.Get())
        m_outerPseudoLayer = m_orderedCaloHitList.rbegin()->first;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::AddIsolatedCaloHit(const CaloHit *const pCaloHit)
{
    if (!m_isolatedCaloHitList.insert(pCaloHit).second)
        return STATUS_CODE_ALREADY_PRESENT;

    const float electromagneticEnergy(pCaloHit->GetElectromagneticEnergy());
    const float hadronicEnergy(pCaloHit->GetHadronicEnergy());

    m_electromagneticEnergy += electromagneticEnergy;
    m_hadronicEnergy += hadronicEnergy;
    m_isolatedElectromagneticEnergy += electromagneticEnergy;
    m_isolatedHadronicEnergy += hadronicEnergy;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::RemoveIsolatedCaloHit(const CaloHit *const pCaloHit)
{
    CaloHitList::iterator iter = m_isolatedCaloHitList.find(pCaloHit);

    if (m_isolatedCaloHitList.end() == iter)
        return STATUS_CODE_NOT_FOUND;

    m_isolatedCaloHitList.erase(iter);

    const float electromagneticEnergy(pCaloHit->GetElectromagneticEnergy());
    const float hadronicEnergy(pCaloHit->GetHadronicEnergy());

    m_electromagneticEnergy -= electromagneticEnergy;
    m_hadronicEnergy -= hadronicEnergy;
    m_isolatedElectromagneticEnergy -= electromagneticEnergy;
    m_isolatedHadronicEnergy -= hadronicEnergy;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const CartesianVector Cluster::GetCentroid(const unsigned int pseudoLayer) const
{
    OrderedCaloHitList::const_iterator iter = m_orderedCaloHitList.find(pseudoLayer);

    if (m_orderedCaloHitList.end() == iter)
        throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

    const float nHitsInLayer(static_cast<float>(iter->second->size()));

    if (0 == nHitsInLayer)
        throw StatusCodeException(STATUS_CODE_FAILURE);

    ValueByPseudoLayerMap::const_iterator xValueIter = m_sumXByPseudoLayer.find(pseudoLayer);
    ValueByPseudoLayerMap::const_iterator yValueIter = m_sumYByPseudoLayer.find(pseudoLayer);
    ValueByPseudoLayerMap::const_iterator zValueIter = m_sumZByPseudoLayer.find(pseudoLayer);

    if ((m_sumXByPseudoLayer.end() == xValueIter) || (m_sumYByPseudoLayer.end() == yValueIter) || (m_sumZByPseudoLayer.end() == zValueIter))
        throw StatusCodeException(STATUS_CODE_FAILURE);

    return CartesianVector(static_cast<float>(xValueIter->second / nHitsInLayer), static_cast<float>(yValueIter->second / nHitsInLayer),
        static_cast<float>(zValueIter->second / nHitsInLayer));
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateFitToAllHitsResult() const
{
    (void) ClusterFitHelper::FitFullCluster(this, m_fitToAllHitsResult);
    m_isFitUpToDate = true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateInitialDirection() const
{
    if (m_orderedCaloHitList.empty())
    {
        m_initialDirection.SetValues(0.f, 0.f, 0.f);
        m_isDirectionUpToDate = false;
        throw StatusCodeException(STATUS_CODE_NOT_INITIALIZED);
    }
    
    CartesianVector initialDirection(0.f, 0.f, 0.f);
    CaloHitList *const pCaloHitList(m_orderedCaloHitList.begin()->second);

    for (CaloHitList::const_iterator iter = pCaloHitList->begin(), iterEnd = pCaloHitList->end(); iter != iterEnd; ++iter)
        initialDirection += (*iter)->GetExpectedDirection();

    m_initialDirection = initialDirection.GetUnitVector();
    m_isDirectionUpToDate = true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateLayerHitType(const unsigned int pseudoLayer, InputHitType &layerHitType) const
{
    OrderedCaloHitList::const_iterator listIter = m_orderedCaloHitList.find(pseudoLayer);

    if ((m_orderedCaloHitList.end() == listIter) || (listIter->second->empty()))
        throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

    HitTypeToEnergyMap hitTypeToEnergyMap;

    for (CaloHitList::const_iterator iter = listIter->second->begin(), iterEnd = listIter->second->end(); iter != iterEnd; ++iter)
    {
        HitTypeToEnergyMap::iterator mapIter = hitTypeToEnergyMap.find((*iter)->GetHitType());

        if (hitTypeToEnergyMap.end() != mapIter)
        {
            mapIter->second += (*iter)->GetHadronicEnergy();
            continue;
        }

        if (!hitTypeToEnergyMap.insert(HitTypeToEnergyMap::value_type((*iter)->GetHitType(), (*iter)->GetHadronicEnergy())).second)
            throw StatusCodeException(STATUS_CODE_FAILURE);
    }

    float highestEnergy(0.f);

    for (HitTypeToEnergyMap::const_iterator iter = hitTypeToEnergyMap.begin(), iterEnd = hitTypeToEnergyMap.end(); iter != iterEnd; ++iter)
    {
        if (iter->second > highestEnergy)
        {
            layerHitType = iter->first;
            highestEnergy = iter->second;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::PerformEnergyCorrections(const Pandora &pandora) const
{
    const EnergyCorrections *const pEnergyCorrections(pandora.GetPlugins()->GetEnergyCorrections());
    const ParticleId *const pParticleId(pandora.GetPlugins()->GetParticleId());

    float correctedElectromagneticEnergy(0.f), correctedHadronicEnergy(0.f), trackComparisonEnergy(0.f);
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, pEnergyCorrections->MakeEnergyCorrections(this, correctedElectromagneticEnergy,
        correctedHadronicEnergy));

    if (pParticleId->IsEmShower(this))
    {
        trackComparisonEnergy = correctedElectromagneticEnergy;
    }
    else
    {
        trackComparisonEnergy = correctedHadronicEnergy;
    }

    if (!(m_correctedElectromagneticEnergy = correctedElectromagneticEnergy) || !(m_correctedHadronicEnergy = correctedHadronicEnergy) ||
        !(m_trackComparisonEnergy = trackComparisonEnergy))
    {
        throw StatusCodeException(STATUS_CODE_FAILURE);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateFastPhotonFlag(const Pandora &pandora) const
{
    const bool fastPhotonFlag(pandora.GetPlugins()->GetParticleId()->IsPhoton(this));

    if (!(m_isPhotonFast = fastPhotonFlag))
        throw StatusCodeException(STATUS_CODE_FAILURE);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateShowerStartLayer(const Pandora &pandora) const
{
    const ShowerProfilePlugin *const pShowerProfilePlugin(pandora.GetPlugins()->GetShowerProfilePlugin());

    unsigned int showerStartLayer(std::numeric_limits<unsigned int>::max());
    pShowerProfilePlugin->CalculateShowerStartLayer(this, showerStartLayer);

    if (!(m_showerStartLayer = showerStartLayer))
        throw StatusCodeException(STATUS_CODE_FAILURE);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::CalculateShowerProfile(const Pandora &pandora) const
{
    const ShowerProfilePlugin *const pShowerProfilePlugin(pandora.GetPlugins()->GetShowerProfilePlugin());

    float showerProfileStart(std::numeric_limits<float>::max()), showerProfileDiscrepancy(std::numeric_limits<float>::max());
    pShowerProfilePlugin->CalculateLongitudinalProfile(this, showerProfileStart, showerProfileDiscrepancy);

    if (!(m_showerProfileStart = showerProfileStart) || !(m_showerProfileDiscrepancy = showerProfileDiscrepancy))
        throw StatusCodeException(STATUS_CODE_FAILURE);
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::ResetProperties()
{
    if (!m_orderedCaloHitList.empty())
        m_orderedCaloHitList.Reset();

    m_isolatedCaloHitList.clear();

    m_nCaloHits = 0;
    m_nPossibleMipHits = 0;

    m_sumXByPseudoLayer.clear();
    m_sumYByPseudoLayer.clear();
    m_sumZByPseudoLayer.clear();

    m_electromagneticEnergy = 0;
    m_hadronicEnergy = 0;

    m_innerPseudoLayer.Reset();
    m_outerPseudoLayer.Reset();

    m_particleId = UNKNOWN_PARTICLE_TYPE;

    this->ResetOutdatedProperties();

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void Cluster::ResetOutdatedProperties()
{
    m_isFitUpToDate = false;
    m_isDirectionUpToDate = false;
    m_initialDirection.SetValues(0.f, 0.f, 0.f);
    m_fitToAllHitsResult.Reset();
    m_showerStartLayer.Reset();
    m_isPhotonFast.Reset();
    m_showerProfileStart.Reset();
    m_showerProfileDiscrepancy.Reset();
    m_correctedElectromagneticEnergy.Reset();
    m_correctedHadronicEnergy.Reset();
    m_trackComparisonEnergy.Reset();
    m_innerLayerHitType.Reset();
    m_outerLayerHitType.Reset();
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::AddHitsFromSecondCluster(const Cluster *const pCluster)
{
    if (this == pCluster)
        return STATUS_CODE_NOT_ALLOWED;

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, m_orderedCaloHitList.Add(orderedCaloHitList));

    const CaloHitList &isolatedCaloHitList(pCluster->GetIsolatedCaloHitList());
    for (CaloHitList::const_iterator iter = isolatedCaloHitList.begin(), iterEnd = isolatedCaloHitList.end(); iter != iterEnd; ++iter)
    {
        if (!m_isolatedCaloHitList.insert(*iter).second)
            return STATUS_CODE_ALREADY_PRESENT;
    }

    this->ResetOutdatedProperties();

    m_nCaloHits += pCluster->GetNCaloHits();
    m_nPossibleMipHits += pCluster->GetNPossibleMipHits();

    m_electromagneticEnergy += pCluster->GetElectromagneticEnergy();
    m_hadronicEnergy += pCluster->GetHadronicEnergy();

    // Loop over pseudo layers in second cluster
    for (OrderedCaloHitList::const_iterator iter = orderedCaloHitList.begin(), iterEnd = orderedCaloHitList.end(); iter != iterEnd; ++iter)
    {
        const unsigned int pseudoLayer(iter->first);
        OrderedCaloHitList::const_iterator currentIter = m_orderedCaloHitList.find(pseudoLayer);

        if ((m_orderedCaloHitList.end() != currentIter) && (currentIter->second->size() > 1))
        {
            m_sumXByPseudoLayer[pseudoLayer] += pCluster->m_sumXByPseudoLayer.at(pseudoLayer);
            m_sumYByPseudoLayer[pseudoLayer] += pCluster->m_sumYByPseudoLayer.at(pseudoLayer);
            m_sumZByPseudoLayer[pseudoLayer] += pCluster->m_sumZByPseudoLayer.at(pseudoLayer);
        }
        else
        {
            m_sumXByPseudoLayer[pseudoLayer] = pCluster->m_sumXByPseudoLayer.at(pseudoLayer);
            m_sumYByPseudoLayer[pseudoLayer] = pCluster->m_sumYByPseudoLayer.at(pseudoLayer);
            m_sumZByPseudoLayer[pseudoLayer] = pCluster->m_sumZByPseudoLayer.at(pseudoLayer);
        }
    }

    m_innerPseudoLayer = m_orderedCaloHitList.begin()->first;
    m_outerPseudoLayer = m_orderedCaloHitList.rbegin()->first;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::AddTrackAssociation(const Track *const pTrack)
{
    if (NULL == pTrack)
        return STATUS_CODE_INVALID_PARAMETER;

    if (!m_associatedTrackList.insert(pTrack).second)
        return STATUS_CODE_FAILURE;

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode Cluster::RemoveTrackAssociation(const Track *const pTrack)
{
    TrackList::iterator iter = m_associatedTrackList.find(pTrack);

    if (m_associatedTrackList.end() == iter)
        return STATUS_CODE_NOT_FOUND;

    m_associatedTrackList.erase(iter);

    return STATUS_CODE_SUCCESS;
}

} // namespace pandora
