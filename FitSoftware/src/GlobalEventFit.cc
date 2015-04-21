/*
 * GlobalEventFit.cc
 *
 *  Created on: Apr 16, 2015
 *      Author: zotz
 *
 *
 *      This class offers an interface for the global event fit including the reconstruction of the hadronic tau and the fit of the di-tau system.
 */

#include "SimpleFits/FitSoftware/interface/GlobalEventFit.h"
#include "SimpleFits/FitSoftware/interface/TauA1NuConstrainedFitter.h"
#include "SimpleFits/FitSoftware/interface/DiTauConstrainedFitter.h"

GlobalEventFit::GlobalEventFit(TrackParticle Muon, LorentzVectorParticle A1, TVector3 PV, TMatrixTSym<double> PVCov){
	isConfigured_ = false;
	isFit_ = false;
	Muon_ = Muon;
	A1_= A1;
	PV_ = PV;
	PVCov_.ResizeTo(PVCov);
	PVCov_= PVCov;
	SV_ = A1.Vertex();
	SVCov_.ResizeTo(A1.VertexCov());
	SVCov_ = A1.VertexCov();

	TPTRObject_ = ThreeProngTauReco();
}
GlobalEventFit::~GlobalEventFit(){

}

TPTRObject GlobalEventFit::ThreeProngTauReco(){
	// Tau Solver
	std::vector<LorentzVectorParticle> Taus;
	std::vector<LorentzVectorParticle> Neutrinos;
	std::vector<bool> recostatus;

	if(A1_.getParMatrix().GetNrows() != LorentzVectorParticle::NLorentzandVertexPar){
	  Logger(Logger::Error) << "A1 is not a valid LorentzVectorParticle." << std::endl;
	  return TPTRObject();
	}

	for(unsigned int Ambiguity = 0; Ambiguity<3; Ambiguity++){
		TauA1NuConstrainedFitter TauA1NU(Ambiguity,A1_,PV_,PVCov_);
		recostatus.push_back(TauA1NU.Fit());
		if(recostatus.at(Ambiguity)){
			//phisign =TauA1NU.GetTauRotationSignificance();
			Taus.push_back(TauA1NU.GetMother());
			LorentzVectorParticle Nu = TauA1NU.GetReFitDaughters().at(1);
			Neutrinos.push_back(Nu);
		}
		else{
			Taus.push_back(LorentzVectorParticle());
			Neutrinos.push_back(LorentzVectorParticle());
		}
	}

	bool isambiguous(IsAmbiguous(recostatus));

	TPTRObject Results = TPTRObject(A1_, Taus, Neutrinos, isambiguous, true);
	isConfigured_ = true;
	return Results;
}

bool GlobalEventFit::IsAmbiguous(std::vector<bool> recostatus){
	if(recostatus.at(0) && !recostatus.at(1) && !recostatus.at(2)) return false;
	else if (!recostatus.at(0) && recostatus.at(1) && recostatus.at(2)) return true;
	else{
		Logger(Logger::Error) << "Three prong tau reconstruction failed." << std::endl;
		return false;
	}
}
GEFObject GlobalEventFit::Fit(){
	if(!isConfigured_) {
		Logger(Logger::Error) << "GlobalEventFit not configured." << std::endl;
		return GEFObject();
	}
	std::vector<bool> recostatus = TPTRObject_.CreateVectorFromAmbiguity();
	std::vector<LorentzVectorParticle> Taus = TPTRObject_.getTaus();
	std::vector< std::vector<LorentzVectorParticle> > InitDaughters, RefitDaughters;
	std::vector<LorentzVectorParticle> InitResonance, FitResonance;
	std::vector<double> Chi2s, Csums, Niterats;
	std::vector<bool> fitstatus;

	for(unsigned Ambiguity = 0; Ambiguity<recostatus.size(); Ambiguity ++){
		if(!recostatus.at(Ambiguity)){
			fitstatus.push_back(false);
			std::vector<LorentzVectorParticle> tmp;
			InitDaughters.push_back(tmp);
			RefitDaughters.push_back(tmp);
			InitResonance.push_back(LorentzVectorParticle());
			FitResonance.push_back(LorentzVectorParticle());
			Chi2s.push_back(-1);
			Csums.push_back(-1);
			Niterats.push_back(-1);
			continue;
		}
		DiTauConstrainedFitter Z2Tau(Taus.at(Ambiguity), Muon_, PV_, PVCov_);
		InitDaughters.push_back(Z2Tau.GetInitialDaughters());

		Z2Tau.SetMaxDelta(1.0);
		Z2Tau.SetNIterMax(100);
		Z2Tau.SetEpsilon(0.01);

		fitstatus.push_back(Z2Tau.Fit());
		if(fitstatus.at(Ambiguity) && Z2Tau.isConverged()){
			FitResonance.push_back(Z2Tau.GetMother());
			InitResonance.push_back(Z2Tau.GetMother());
			RefitDaughters.push_back(Z2Tau.GetReFitDaughters());
			Chi2s.push_back(Z2Tau.ChiSquare());
			Niterats.push_back(Z2Tau.NIter());
			Csums.push_back(Z2Tau.CSum());
		}
		else{
			fitstatus.push_back(false);
			std::vector<LorentzVectorParticle> tmp;
			InitDaughters.push_back(tmp);
			RefitDaughters.push_back(tmp);
			Chi2s.push_back(-1);
			Csums.push_back(-1);
			Niterats.push_back(-1);
		}
	}
	int IndexToReturn(-1);
	if(AmbiguitySolverByChi2(recostatus, fitstatus, Chi2s, IndexToReturn)){
		GEFObject Results = GEFObject(InitDaughters.at(IndexToReturn),
			InitResonance.at(IndexToReturn),
			RefitDaughters.at(IndexToReturn),
			FitResonance.at(IndexToReturn),
			true, Chi2s.at(IndexToReturn), Csums.at(IndexToReturn), Niterats.at(IndexToReturn), IndexToReturn);
		isFit_ = true;
		return Results;
	}
	else{
		Logger(Logger::Verbose) << "Fit failed: Ambiguity was not solvable" << std::endl;
		return GEFObject();
	}
}

bool GlobalEventFit::AmbiguitySolverByChi2(std::vector<bool> A1Fit, std::vector<bool> EventFit, std::vector<double> Chi2s, int &IndexToReturn){

	if(EventFit.at(0) == true && EventFit.at(1) == false && EventFit.at(2) == false && Chi2s.at(0) > 0){IndexToReturn =0; return true;}
	if(EventFit.at(1) == true && EventFit.at(2) == false && Chi2s.at(1) > 0){ IndexToReturn = 1;return true;}
	if(EventFit.at(1) == false && EventFit.at(2) == true && Chi2s.at(2) > 0){ IndexToReturn = 2;return true;}

	if((A1Fit.at(1) == true && A1Fit.at(2) == true) && (EventFit.at(1) == true && EventFit.at(2) == true)){
		if(Chi2s.at(1) < Chi2s.at(2) && Chi2s.at(1) > 0){ IndexToReturn =1;return true;}
		if(Chi2s.at(1) > Chi2s.at(2) && Chi2s.at(2) > 0){ IndexToReturn =2;return true;}
	}
	return false;
}