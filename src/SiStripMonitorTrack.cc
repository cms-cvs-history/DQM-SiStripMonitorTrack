#include <FWCore/MessageLogger/interface/MessageLogger.h>

#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/GluedGeomDet.h"

#include "CalibTracker/Records/interface/SiStripDetCablingRcd.h"
#include "CondFormats/DataRecord/interface/SiStripNoisesRcd.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/SiStripDetId/interface/TECDetId.h"
#include "DataFormats/SiStripDetId/interface/TIBDetId.h"
#include "DataFormats/SiStripDetId/interface/TIDDetId.h"
#include "DataFormats/SiStripDetId/interface/TOBDetId.h"
#include "DataFormats/TrackerRecHit2D/interface/SiStripRecHit2D.h"
#include "DataFormats/TrackerRecHit2D/interface/ProjectedSiStripRecHit2D.h"
#include "DataFormats/TrackerRecHit2D/interface/SiStripMatchedRecHit2D.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

#include "DQM/SiStripMonitorTrack/interface/SiStripMonitorTrack.h"

#include "DQM/SiStripCommon/interface/SiStripHistoId.h"
#include "TMath.h"

static const uint16_t _NUM_SISTRIP_SUBDET_ = 4;
static TString SubDet[_NUM_SISTRIP_SUBDET_]={"TIB","TID","TOB","TEC"};
static std::string flags[2] = {"OnTrack","OffTrack"};

SiStripMonitorTrack::SiStripMonitorTrack(const edm::ParameterSet& conf):
  dbe(edm::Service<DQMStore>().operator->()),
  conf_(conf),
  Cluster_src_( conf.getParameter<edm::InputTag>( "Cluster_src" ) ),
  Mod_On_(conf.getParameter<bool>("Mod_On")),
  Trend_On_(conf.getParameter<bool>("Trend_On")),
  OffHisto_On_(conf.getParameter<bool>("OffHisto_On")),
  folder_organizer(), tracksCollection_in_EventTree(true),
  firstEvent(-1),
  neighbourStripNumber(3)
{
  for(int i=0;i<4;++i) for(int j=0;j<2;++j) NClus[i][j]=0;
  if(OffHisto_On_){
    off_Flag = 2;
  }else{
    off_Flag = 1;
  }
}

//------------------------------------------------------------------------
SiStripMonitorTrack::~SiStripMonitorTrack() { }

//------------------------------------------------------------------------
void SiStripMonitorTrack::beginRun(const edm::Run& run,const edm::EventSetup& es)
{
  //get geom 
  es.get<TrackerDigiGeometryRecord>().get( tkgeom );
  edm::LogInfo("SiStripMonitorTrack") << "[SiStripMonitorTrack::beginRun] There are "<<tkgeom->detUnits().size() <<" detectors instantiated in the geometry" << std::endl;  
  es.get<SiStripDetCablingRcd>().get( SiStripDetCabling_ );
  es.get<SiStripNoisesRcd>().get(noiseHandle_);
  book();
}

//------------------------------------------------------------------------
void SiStripMonitorTrack::endJob(void)
{
  if(conf_.getParameter<bool>("OutputMEsInRootFile")){
    dbe->showDirStructure();
    dbe->save(conf_.getParameter<std::string>("OutputFileName"));
  }
}

// ------------ method called to produce the data  ------------
void SiStripMonitorTrack::analyze(const edm::Event& e, const edm::EventSetup& es)
{
  tracksCollection_in_EventTree=true;
  trackAssociatorCollection_in_EventTree=true;
  
  //initialization of global quantities
  edm::LogInfo("SiStripMonitorTrack") << "[SiStripMonitorTrack::analyse]  " << "Run " << e.id().run() << " Event " << e.id().event() << std::endl;
  runNb   = e.id().run();
  eventNb = e.id().event();
  vPSiStripCluster.clear();
  countOn=0;
  countOff=0;

  e.getByLabel( Cluster_src_, dsv_SiStripCluster); 

  // track input  
  std::string TrackProducer = conf_.getParameter<std::string>("TrackProducer");
  std::string TrackLabel = conf_.getParameter<std::string>("TrackLabel");
  
  e.getByLabel(TrackProducer, TrackLabel, trackCollection);//takes the track collection
 
  if (trackCollection.isValid()){
  }else{
    edm::LogError("SiStripMonitorTrack")<<" Track Collection is not valid !! " << TrackLabel<<std::endl;
    tracksCollection_in_EventTree=false;
  }
  
  // get RawDigi collection
  std::string digiProducer = conf_.getParameter<std::string>("RawDigiProducer");
  std::string    digiLabel = conf_.getParameter<std::string>("RawDigiLabel");
  e.getByLabel(digiProducer,digiLabel,dsv_SiStripRawDigi);
  if( ! dsv_SiStripRawDigi.failedToGet() ){
    // digi check
    edm::LogInfo("SiStripMonitorTrack") << "[SiStripMonitorTrack::analyse]  " << "Digi " << digiLabel
					<< " found " << dsv_SiStripRawDigi->size() << "\n";
    /* digi check
       for( edm::DetSetVector<SiStripRawDigi>::const_iterator iDigi = dsv_SiStripRawDigi->begin();
       iDigi != dsv_SiStripRawDigi->end(); ++iDigi)
       edm::LogInfo("SiStripMonitorTrack") << "\n\t Digi in detid " << iDigi->detId() << ": " << iDigi->size();
    */
  }else{
    LogDebug("SiStripMonitorTrack")<<"RawDigis not found "<<std::endl;
  }
  // trajectory input
  e.getByLabel(TrackProducer, TrackLabel, TrajectoryCollection);
  e.getByLabel(TrackProducer, TrackLabel, TItkAssociatorCollection);
  if( TItkAssociatorCollection.isValid()){
  }else{
    edm::LogError("SiStripMonitorTrack")<<"Association not found "<<std::endl;
    trackAssociatorCollection_in_EventTree=false;
  }
  
  //Perform track study
  if (tracksCollection_in_EventTree && trackAssociatorCollection_in_EventTree) trackStudy(es);
  
  //Perform Cluster Study (irrespectively to tracks)
  if (dsv_SiStripCluster.isValid() && OffHisto_On_){
    AllClusters(es);//analyzes the off Track Clusters
  }else{
    edm::LogError("SiStripMonitorTrack")<< "ClusterCollection is not valid!!" << std::endl;
  }

  //Summary Counts of clusters
  std::map<TString, MonitorElement*>::iterator iME;
  std::map<TString, ModMEs>::iterator          iModME ;
  for (int j=0;j<off_Flag;++j){ // loop over ontrack, offtrack
    int nTot=0;
    for (int i=0;i<4;++i){ // loop over TIB, TID, TOB, TEC
      name=flags[j]+"_in_"+SubDet[i];      
      iModME = ModMEsMap.find(name);
      if(iModME!=ModMEsMap.end()) {
	if(flags[j]=="OnTrack" && NClus[i][j]){
	  fillME(   iModME->second.nClusters,      NClus[i][j]);
	}else if(flags[j]=="OffTrack"){
	  fillME(   iModME->second.nClusters,      NClus[i][j]);
	}
	if(Trend_On_)
	  fillTrend(iModME->second.nClustersTrend, NClus[i][j]);
      }
      nTot+=NClus[i][j];
      NClus[i][j]=0;
    }// loop over TIB, TID, TOB, TEC
  
    name=flags[j]+"_NumberOfClusters";
    iME = MEMap.find(name);
    if(iME!=MEMap.end() && nTot) iME->second->Fill(nTot);
	if(Trend_On_){
	  iME = MEMap.find(name+"Trend");
	  if(iME!=MEMap.end() && nTot) fillTrend(iME->second,nTot);
	}
 } // loop over ontrack, offtrack
  
}

//------------------------------------------------------------------------  
void SiStripMonitorTrack::book() 
{
  
  std::vector<uint32_t> vdetId_;
  SiStripDetCabling_->addActiveDetectorsRawIds(vdetId_);
  //Histos for each detector, layer and module
  for (std::vector<uint32_t>::const_iterator detid_iter=vdetId_.begin();detid_iter!=vdetId_.end();detid_iter++){  //loop on all the active detid
    uint32_t detid = *detid_iter;
    
    if (detid < 1){
      edm::LogError("SiStripMonitorTrack")<< "[" <<__PRETTY_FUNCTION__ << "] invalid detid " << detid<< std::endl;
      continue;
    }
    if (DetectedLayers.find(folder_organizer.GetSubDetAndLayer(detid)) == DetectedLayers.end()){
      
      DetectedLayers[folder_organizer.GetSubDetAndLayer(detid)]=true;
    }    

    // set the DQM directory
    std::string MEFolderName = conf_.getParameter<std::string>("FolderName");    
    dbe->setCurrentFolder(MEFolderName);
    
    for (int j=0;j<off_Flag;j++) { // Loop on onTrack, offTrack
      name=flags[j]+"_NumberOfClusters";
      if(MEMap.find(name)==MEMap.end()) {
	MEMap[name]=bookME1D("TH1nClusters", name.Data()); 
	if(Trend_On_){
	  name+="Trend";
	  MEMap[name]=bookMETrend("TH1nClusters", name.Data());
	}      
      }
    }// End Loop on onTrack, offTrack
    
    // 	LogTrace("SiStripMonitorTrack") << " Detid " << *detid 
    //					<< " SubDet " << GetSubDetAndLayer(*detid).first 
    //					<< " Layer "  << GetSubDetAndLayer(*detid).second;

    // book Layer plots      
    for (int j=0;j<off_Flag;j++){ 
      folder_organizer.setLayerFolder(*detid_iter,folder_organizer.GetSubDetAndLayer(*detid_iter).second); 
      bookTrendMEs("layer",folder_organizer.GetSubDetAndLayer(*detid_iter).second,*detid_iter,flags[j]);
    }
  
    if(Mod_On_){
      //    book module plots
      folder_organizer.setDetectorFolder(*detid_iter);
      bookModMEs("det",*detid_iter);
    }
    DetectedLayers[folder_organizer.GetSubDetAndLayer(*detid_iter)] |= (DetectedLayers.find(folder_organizer.GetSubDetAndLayer(*detid_iter)) == DetectedLayers.end());
    //      }
  }//end loop on detector
  
  //  book SubDet plots
  for (std::map<std::pair<std::string,int32_t>,bool>::const_iterator iter=DetectedLayers.begin(); iter!=DetectedLayers.end();iter++){
    for (int j=0;j<off_Flag;j++){ // Loop on onTrack, offTrack
      folder_organizer.setDetectorFolder(0);
      dbe->cd("SiStrip/MechanicalView/"+iter->first.first);
      name=flags[j]+"_in_"+iter->first.first; 
      bookSubDetMEs(name,flags[j]); //for subdets
    }//end loop on onTrack,offTrack
  }  
}
  
//--------------------------------------------------------------------------------
void SiStripMonitorTrack::bookModMEs(TString name, uint32_t id)//Histograms at MODULE level
{
  SiStripHistoId hidmanager;
  std::string hid = hidmanager.createHistoId("",name.Data(),id);
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(TString(hid));
  if(iModME==ModMEsMap.end()){
    ModMEs theModMEs; 
    //Cluster Width
    theModMEs.ClusterWidth=bookME1D("TH1ClusterWidth", hidmanager.createHistoId("OnTrack_cWidth",name.Data(),id).c_str()); 
    dbe->tag(theModMEs.ClusterWidth,id); 
    // Symmetric Eta function Eta=(L+R)/(L+R+S) (Capacitive Coupling)
    //    theModMEs.ClusterSymmEtaCC=bookME1D("TH1ClusterSymmEtaCC", hidmanager.createHistoId("OnTrack_cSymmEtaCC",name.Data(),id).c_str()); 
    //    dbe->tag(theModMEs.ClusterSymmEtaCC,id); 
    //Cluster Charge
    theModMEs.ClusterCharge=bookME1D("TH1ClusterCharge", hidmanager.createHistoId("OnTrack_cCharge",name.Data(),id).c_str());
    dbe->tag(theModMEs.ClusterCharge,id); 
    //Cluster StoN
    theModMEs.ClusterStoN=bookME1D("TH1ClusterStoN", hidmanager.createHistoId("OnTrack_cStoN",name.Data(),id).c_str());
    dbe->tag(theModMEs.ClusterStoN,id); 
    //Cluster Charge Corrected
    theModMEs.ClusterChargeCorr=bookME1D("TH1ClusterChargeCorr", hidmanager.createHistoId("OnTrack_cChargeCorr",name.Data(),id).c_str());
    dbe->tag(theModMEs.ClusterChargeCorr,id); 
    //Cluster StoN Corrected
    theModMEs.ClusterStoNCorr=bookME1D("TH1ClusterStoNCorr", hidmanager.createHistoId("OnTrack_cStoNCorr",name.Data(),id).c_str());
    dbe->tag(theModMEs.ClusterStoNCorr,id); 
    //Cluster Position
    theModMEs.ClusterPos=bookME1D("TH1ClusterPos", hidmanager.createHistoId("OnTrack_cPos",name.Data(),id).c_str());  
    dbe->tag(theModMEs.ClusterPos,id); 
    //Cluster PGV
    theModMEs.ClusterPGV=bookMEProfile("TProfileClusterPGV", hidmanager.createHistoId("OnTrack_cPGV",name.Data(),id).c_str()); 
    dbe->tag(theModMEs.ClusterPGV,id); 
    //bookeeping
    ModMEsMap[hid]=theModMEs;
  }
}

void SiStripMonitorTrack::bookTrendMEs(TString name,int32_t layer,uint32_t id,std::string flag)//Histograms and Trends at LAYER LEVEL
{
  char rest[1024];
  int subdetid = ((id>>25)&0x7);
  if(       subdetid==3 ){
  // ---------------------------  TIB  --------------------------- //
    TIBDetId tib1 = TIBDetId(id);
    sprintf(rest,"TIB__layer__%d",tib1.layer());
  }else if( subdetid==4){
    // ---------------------------  TID  --------------------------- //
    TIDDetId tid1 = TIDDetId(id);
    sprintf(rest,"TID__side__%d__wheel__%d",tid1.side(),tid1.wheel());
  }else if( subdetid==5){
    // ---------------------------  TOB  --------------------------- //
    TOBDetId tob1 = TOBDetId(id);
    sprintf(rest,"TOB__layer__%d",tob1.layer());
  }else if( subdetid==6){
    // ---------------------------  TEC  --------------------------- //
    TECDetId tec1 = TECDetId(id);
    sprintf(rest,"TEC__side__%d__wheel__%d",tec1.side(),tec1.wheel());
  }else{
    // ---------------------------  ???  --------------------------- //
    edm::LogError("SiStripTkDQM|WrongInput")<<"no such subdetector type :"<<subdetid<<" no folder set!"<<std::endl;
    return;
  }
  
  SiStripHistoId hidmanager;
  std::string hid = hidmanager.createHistoLayer("",name.Data(),rest,flag);
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(TString(hid));
  if(iModME==ModMEsMap.end()){
    ModMEs theModMEs; 
    //Cluster Width
    theModMEs.ClusterWidth=bookME1D("TH1ClusterWidth", hidmanager.createHistoLayer("Summary_cWidth",name.Data(),rest,flag).c_str()); 
    dbe->tag(theModMEs.ClusterWidth,layer); 
    // Symmetric Eta function Eta=(L+R)/(L+R+S) (Capacitive Coupling)
    theModMEs.ClusterSymmEtaCC=bookME1D("TH1ClusterSymmEtaCC", hidmanager.createHistoLayer("Summary_cSymmEtaCC",name.Data(),rest,flag).c_str()); 
    dbe->tag(theModMEs.ClusterSymmEtaCC,layer); 
    //Trends
    if(Trend_On_){
      theModMEs.ClusterWidthTrend=bookMETrend("TH1ClusterWidth", hidmanager.createHistoLayer("Trend_cWidth",name.Data(),rest,flag).c_str()); 
      dbe->tag(theModMEs.ClusterWidthTrend,layer); 
      theModMEs.ClusterSymmEtaCCTrend=bookMETrend("TH1ClusterSymmEtaCC", hidmanager.createHistoLayer("Trend_cSymmEtaCC",name.Data(),rest,flag).c_str()); 
      dbe->tag(theModMEs.ClusterSymmEtaCCTrend,layer); 
      theModMEs.ClusterNoiseTrend=bookMETrend("TH1ClusterNoise", hidmanager.createHistoLayer("Trend_cNoise",name.Data(),rest,flag).c_str()); 
      dbe->tag(theModMEs.ClusterNoiseTrend,layer); 
      theModMEs.ClusterChargeTrend=bookMETrend("TH1ClusterCharge", hidmanager.createHistoLayer("Trend_cCharge",name.Data(),rest,flag).c_str());
      dbe->tag(theModMEs.ClusterChargeTrend,layer); 
      theModMEs.ClusterStoNTrend=bookMETrend("TH1ClusterStoN", hidmanager.createHistoLayer("Trend_cStoN",name.Data(),rest,flag).c_str());
      dbe->tag(theModMEs.ClusterStoNTrend,layer); 
    }
    
    //Cluster Noise
    theModMEs.ClusterNoise=bookME1D("TH1ClusterNoise", hidmanager.createHistoLayer("Summary_cNoise",name.Data(),rest,flag).c_str()); 
    dbe->tag(theModMEs.ClusterNoise,layer); 
    
    //Cluster Charge
    theModMEs.ClusterCharge=bookME1D("TH1ClusterCharge", hidmanager.createHistoLayer("Summary_cCharge",name.Data(),rest,flag).c_str());
    dbe->tag(theModMEs.ClusterCharge,layer);
    
    //Cluster StoN
    theModMEs.ClusterStoN=bookME1D("TH1ClusterStoN", hidmanager.createHistoLayer("Summary_cStoN",name.Data(),rest,flag).c_str());
    dbe->tag(theModMEs.ClusterStoN,layer); 
    if(flag=="OnTrack"){
      //Cluster Charge Corrected
      theModMEs.ClusterChargeCorr=bookME1D("TH1ClusterChargeCorr", hidmanager.createHistoLayer("Summary_cChargeCorr",name.Data(),rest,flag).c_str());
      dbe->tag(theModMEs.ClusterChargeCorr,layer); 
      //Cluster StoN Corrected
      theModMEs.ClusterStoNCorr=bookME1D("TH1ClusterStoNCorr", hidmanager.createHistoLayer("Summary_cStoNCorr",name.Data(),rest,flag).c_str());
      dbe->tag(theModMEs.ClusterStoNCorr,layer); 
      
      if(Trend_On_){
	theModMEs.ClusterChargeCorrTrend=bookMETrend("TH1ClusterChargeCorr", hidmanager.createHistoLayer("Trend_cChargeCorr",name.Data(),rest,flag).c_str());
	dbe->tag(theModMEs.ClusterChargeCorrTrend,layer); 
	theModMEs.ClusterStoNCorrTrend=bookMETrend("TH1ClusterStoNCorr", hidmanager.createHistoLayer("Trend_cStoNCorr",name.Data(),rest,flag).c_str());
	dbe->tag(theModMEs.ClusterStoNCorrTrend,layer); 
      }
      
    }
    //Cluster Position
    theModMEs.ClusterPos=bookME1D("TH1ClusterPos", hidmanager.createHistoLayer("Summary_cPos",name.Data(),rest,flag).c_str());  
    dbe->tag(theModMEs.ClusterPos,layer); 
    //bookeeping
    ModMEsMap[hid]=theModMEs;
  }
  
}

void SiStripMonitorTrack::bookSubDetMEs(TString name,TString flag)//Histograms at SubDet level
{
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(name);
  char completeName[1024];
  if(iModME==ModMEsMap.end()){
    ModMEs theModMEs; 
    //Number of Cluster 
    sprintf(completeName,"Summary_NumberOfClusters_%s",name.Data());
    theModMEs.nClusters=bookME1D("TH1nClusters", completeName);
    //Cluster Width
    sprintf(completeName,"Summary_cWidth_%s",name.Data());
    theModMEs.ClusterWidth=bookME1D("TH1ClusterWidth", completeName);
    // Symmetric Eta function Eta=(L+R)/(L+R+S) (Capacitive Coupling)
    sprintf(completeName,"Summary_cSymmEtaCC_%s",name.Data());
    theModMEs.ClusterSymmEtaCC=bookME1D("TH1ClusterSymmEtaCC", completeName);
    //Cluster Noise
    sprintf(completeName,"Summary_cNoise_%s",name.Data());
    theModMEs.ClusterNoise=bookME1D("TH1ClusterNoise", completeName);
    //Cluster Charge
    sprintf(completeName,"Summary_cCharge_%s",name.Data());
    theModMEs.ClusterCharge=bookME1D("TH1ClusterCharge", completeName);
    //Cluster StoN
    sprintf(completeName,"Summary_cStoN_%s",name.Data());
    theModMEs.ClusterStoN=bookME1D("TH1ClusterStoN", completeName);
    if(Trend_On_){
      sprintf(completeName,"Trend_NumberOfClusters_%s",name.Data());
      theModMEs.nClustersTrend=bookMETrend("TH1nClusters", completeName);
      sprintf(completeName,"Trend_cWidth_%s",name.Data());
      theModMEs.ClusterWidthTrend=bookMETrend("TH1ClusterWidth", completeName);
      sprintf(completeName,"Trend_cNoise_%s",name.Data());
      sprintf(completeName,"Trend_cSymmEtaCC_%s",name.Data());
      theModMEs.ClusterSymmEtaCCTrend=bookMETrend("TH1ClusterSymmEtaCC", completeName);
      theModMEs.ClusterNoiseTrend=bookMETrend("TH1ClusterNoise", completeName);
      sprintf(completeName,"Trend_cCharge_%s",name.Data());
      theModMEs.ClusterChargeTrend=bookMETrend("TH1ClusterCharge", completeName);
      sprintf(completeName,"Trend_cStoN_%s",name.Data());
      theModMEs.ClusterStoNTrend=bookMETrend("TH1ClusterStoN", completeName); 
    }
    
    if (flag=="OnTrack"){    //Cluster StoNCorr
      
      sprintf(completeName,"Summary_cStoNCorr_%s",name.Data());
      theModMEs.ClusterStoNCorr=bookME1D("TH1ClusterStoNCorr", completeName);
      sprintf(completeName,"Summary_cChargeCorr_%s",name.Data());
      theModMEs.ClusterChargeCorr=bookME1D("TH1ClusterChargeCorr", completeName);
      
      if(Trend_On_){ 
	sprintf(completeName,"Trend_cStoNCorr_%s",name.Data());
	theModMEs.ClusterStoNCorrTrend=bookMETrend("TH1ClusterStoNCorr", completeName);     
	//Cluster ChargeCorr
	sprintf(completeName,"Trend_cChargeCorr_%s",name.Data());
	theModMEs.ClusterChargeCorrTrend=bookMETrend("TH1ClusterChargeCorr", completeName);
      }
    }
    //bookeeping
    ModMEsMap[name]=theModMEs;
  }
}
//--------------------------------------------------------------------------------

MonitorElement* SiStripMonitorTrack::bookME1D(const char* ParameterSetLabel, const char* HistoName)
{
  Parameters =  conf_.getParameter<edm::ParameterSet>(ParameterSetLabel);
  return dbe->book1D(HistoName,HistoName,
		     Parameters.getParameter<int32_t>("Nbinx"),
		     Parameters.getParameter<double>("xmin"),
		     Parameters.getParameter<double>("xmax")
		     );
}

//--------------------------------------------------------------------------------
MonitorElement* SiStripMonitorTrack::bookME2D(const char* ParameterSetLabel, const char* HistoName)
{
  Parameters =  conf_.getParameter<edm::ParameterSet>(ParameterSetLabel);
  return dbe->book2D(HistoName,HistoName,
		     Parameters.getParameter<int32_t>("Nbinx"),
		     Parameters.getParameter<double>("xmin"),
		     Parameters.getParameter<double>("xmax"),
		     Parameters.getParameter<int32_t>("Nbiny"),
		     Parameters.getParameter<double>("ymin"),
		     Parameters.getParameter<double>("ymax")
		     );
}

//--------------------------------------------------------------------------------
MonitorElement* SiStripMonitorTrack::bookME3D(const char* ParameterSetLabel, const char* HistoName)
{
  Parameters =  conf_.getParameter<edm::ParameterSet>(ParameterSetLabel);
  return dbe->book3D(HistoName,HistoName,
		     Parameters.getParameter<int32_t>("Nbinx"),
		     Parameters.getParameter<double>("xmin"),
		     Parameters.getParameter<double>("xmax"),
		     Parameters.getParameter<int32_t>("Nbiny"),
		     Parameters.getParameter<double>("ymin"),
		     Parameters.getParameter<double>("ymax"),
		     Parameters.getParameter<int32_t>("Nbinz"),
		     Parameters.getParameter<double>("zmin"),
		     Parameters.getParameter<double>("zmax")
		     );
}

//--------------------------------------------------------------------------------
MonitorElement* SiStripMonitorTrack::bookMEProfile(const char* ParameterSetLabel, const char* HistoName)
{
  Parameters =  conf_.getParameter<edm::ParameterSet>(ParameterSetLabel);
  return dbe->bookProfile(HistoName,HistoName,
			  Parameters.getParameter<int32_t>("Nbinx"),
			  Parameters.getParameter<double>("xmin"),
			  Parameters.getParameter<double>("xmax"),
			  Parameters.getParameter<int32_t>("Nbiny"),
			  Parameters.getParameter<double>("ymin"),
			  Parameters.getParameter<double>("ymax"),
			  "" );
}

//--------------------------------------------------------------------------------
MonitorElement* SiStripMonitorTrack::bookMETrend(const char* ParameterSetLabel, const char* HistoName)
{
  Parameters =  conf_.getParameter<edm::ParameterSet>(ParameterSetLabel);
  edm::ParameterSet ParametersTrend =  conf_.getParameter<edm::ParameterSet>("Trending");
  MonitorElement* me = dbe->bookProfile(HistoName,HistoName,
					ParametersTrend.getParameter<int32_t>("Nbins"),
					0,
					ParametersTrend.getParameter<int32_t>("Nbins"),
					100, //that parameter should not be there !?
					Parameters.getParameter<double>("xmin"),
					Parameters.getParameter<double>("xmax"),
					"" );
  if(!me) return me;
  char buffer[256];
  sprintf(buffer,"EventId/%d",ParametersTrend.getParameter<int32_t>("Steps"));
  me->setAxisTitle(std::string(buffer),1);
  return me;
}

//------------------------------------------------------------------------------------------
void SiStripMonitorTrack::trackStudy(const edm::EventSetup& es)
{
  
  LogDebug("SiStripMonitorTrack") << "inside trackstudy" << std::endl;
  const reco::TrackCollection tC = *(trackCollection.product());
  int i=0;
  std::vector<TrajectoryMeasurement> measurements;
  for(TrajTrackAssociationCollection::const_iterator it =  TItkAssociatorCollection->begin();it !=  TItkAssociatorCollection->end(); ++it){
    const edm::Ref<std::vector<Trajectory> > traj_iterator = it->key;  
    // Trajectory Map, extract Trajectory for this track
    reco::TrackRef trackref = it->val;
    LogTrace("SiStripMonitorTrack")
      << "Track number "<< i+1 
      << "\n\tmomentum: " << trackref->momentum()
      << "\n\tPT: " << trackref->pt()
      << "\n\tvertex: " << trackref->vertex()
      << "\n\timpact parameter: " << trackref->d0()
      << "\n\tcharge: " << trackref->charge()
      << "\n\tnormalizedChi2: " << trackref->normalizedChi2() 
      <<"\n\tFrom EXTRA : "
      <<"\n\t\touter PT "<< trackref->outerPt()<<std::endl;
    i++;
    
    measurements =traj_iterator->measurements();
    std::vector<TrajectoryMeasurement>::iterator traj_mes_iterator;
    int nhit=0;
    for(traj_mes_iterator=measurements.begin();traj_mes_iterator!=measurements.end();traj_mes_iterator++){//loop on measurements
      //trajectory local direction and position on detector
      LocalPoint  stateposition;
      LocalVector statedirection;
      
      TrajectoryStateOnSurface  updatedtsos=traj_mes_iterator->updatedState();
      ConstRecHitPointer ttrh=traj_mes_iterator->recHit();
      if (!ttrh->isValid()) {continue;}
      
      std::stringstream ss;
      
      nhit++;
      
      const ProjectedSiStripRecHit2D* phit=dynamic_cast<const ProjectedSiStripRecHit2D*>( ttrh->hit() );
      const SiStripMatchedRecHit2D* matchedhit=dynamic_cast<const SiStripMatchedRecHit2D*>( ttrh->hit() );
      const SiStripRecHit2D* hit=dynamic_cast<const SiStripRecHit2D*>( ttrh->hit() );	
      
      RecHitType type=Single;
      
      if(matchedhit){
	LogTrace("SiStripMonitorTrack")<<"\nMatched recHit found"<< std::endl;
	type=Matched;
	
	GluedGeomDet * gdet=(GluedGeomDet *)tkgeom->idToDet(matchedhit->geographicalId());
	GlobalVector gtrkdirup=gdet->toGlobal(updatedtsos.localMomentum());	    
	//mono side
	const GeomDetUnit * monodet=gdet->monoDet();
	statedirection=monodet->toLocal(gtrkdirup);
	if(statedirection.mag() != 0)	  RecHitInfo(matchedhit->monoHit(),statedirection,trackref,es);
	//stereo side
	const GeomDetUnit * stereodet=gdet->stereoDet();
	statedirection=stereodet->toLocal(gtrkdirup);
	if(statedirection.mag() != 0)	  RecHitInfo(matchedhit->stereoHit(),statedirection,trackref,es);
	ss<<"\nLocalMomentum (stereo): " <<  statedirection;
      }
      else if(phit){
	LogTrace("SiStripMonitorTrack")<<"\nProjected recHit found"<< std::endl;
	type=Projected;
	GluedGeomDet * gdet=(GluedGeomDet *)tkgeom->idToDet(phit->geographicalId());
	
	GlobalVector gtrkdirup=gdet->toGlobal(updatedtsos.localMomentum());
	const SiStripRecHit2D&  originalhit=phit->originalHit();
	const GeomDetUnit * det;
	if(!StripSubdetector(originalhit.geographicalId().rawId()).stereo()){
	  //mono side
	  LogTrace("SiStripMonitorTrack")<<"\nProjected recHit found  MONO"<< std::endl;
	  det=gdet->monoDet();
	  statedirection=det->toLocal(gtrkdirup);
	  if(statedirection.mag() != 0) RecHitInfo(&(phit->originalHit()),statedirection,trackref,es);
	}
	else{
	  LogTrace("SiStripMonitorTrack")<<"\nProjected recHit found STEREO"<< std::endl;
	  //stereo side
	  det=gdet->stereoDet();
	  statedirection=det->toLocal(gtrkdirup);
	  if(statedirection.mag() != 0) RecHitInfo(&(phit->originalHit()),statedirection,trackref,es);
	}
      }else {
	if(hit!=0){
	  ss<<"\nSingle recHit found"<< std::endl;	  
	  statedirection=updatedtsos.localMomentum();
	  if(statedirection.mag() != 0) RecHitInfo(hit,statedirection,trackref,es);
	}
      }
      ss <<"LocalMomentum: "<<statedirection
	 << "\nLocal x-z plane angle: "<<atan2(statedirection.x(),statedirection.z());	      
      LogTrace("SiStripMonitorTrack") <<ss.str() << std::endl;
    }
    
  }
}

void SiStripMonitorTrack::RecHitInfo(const SiStripRecHit2D* tkrecHit, LocalVector LV,reco::TrackRef track_ref, const edm::EventSetup& es){
  
  if(!tkrecHit->isValid()){
    LogTrace("SiStripMonitorTrack") <<"\t\t Invalid Hit " << std::endl;
    return;  
  }
  
  const uint32_t& detid = tkrecHit->geographicalId().rawId();
  if (find(ModulesToBeExcluded_.begin(),ModulesToBeExcluded_.end(),detid)!=ModulesToBeExcluded_.end()){
    LogTrace("SiStripMonitorTrack") << "Modules Excluded" << std::endl;
    return;
  }
  
  LogTrace("SiStripMonitorTrack")
    <<"\n\t\tRecHit on det "<<tkrecHit->geographicalId().rawId()
    <<"\n\t\tRecHit in LP "<<tkrecHit->localPosition()
    <<"\n\t\tRecHit in GP "<<tkgeom->idToDet(tkrecHit->geographicalId())->surface().toGlobal(tkrecHit->localPosition()) 
    <<"\n\t\tRecHit trackLocal vector "<<LV.x() << " " << LV.y() << " " << LV.z() <<std::endl; 
  
  //Get SiStripCluster from SiStripRecHit
  if ( tkrecHit != NULL ){
    LogTrace("SiStripMonitorTrack") << "GOOD hit" << std::endl;
    const SiStripCluster* SiStripCluster_ = &*(tkrecHit->cluster());
    if ( clusterInfos(&*SiStripCluster_,detid,"OnTrack", LV ) ) {
      vPSiStripCluster.push_back(SiStripCluster_);//this is the vector of the clusters on track
      countOn++;
    }
  }else{
    LogTrace("SiStripMonitorTrack") << "NULL hit" << std::endl;
  }	  
}

//------------------------------------------------------------------------
void SiStripMonitorTrack::AllClusters( const edm::EventSetup& es)
{
  
  //Loop on Dets
  edm::DetSetVector<SiStripCluster>::const_iterator DSViter=dsv_SiStripCluster->begin();
  for (; DSViter!=dsv_SiStripCluster->end();DSViter++){
    uint32_t detid=DSViter->id;
    if (find(ModulesToBeExcluded_.begin(),ModulesToBeExcluded_.end(),detid)!=ModulesToBeExcluded_.end()) continue;
    //Loop on Clusters
     edm::LogInfo("SiStripMonitorTrack") << "on detid "<< detid << " N Cluster= " << DSViter->data.size();
    edm::DetSet<SiStripCluster>::const_iterator ClusIter = DSViter->data.begin();
    for(; ClusIter!=DSViter->data.end(); ClusIter++) {
      LogDebug("SiStripMonitorTrack") << "ClusIter " << &*ClusIter << "\t " 
				      << std::find(vPSiStripCluster.begin(),vPSiStripCluster.end(),&*ClusIter)-vPSiStripCluster.begin();
      if (std::find(vPSiStripCluster.begin(),vPSiStripCluster.end(),&*ClusIter) == vPSiStripCluster.end()){
	const SiStripCluster* SiStripCluster_ = &*ClusIter;
	if ( clusterInfos(SiStripCluster_,detid,"OffTrack",LV) ) {
	  countOff++;
	}
      }
    }
  }
}

//------------------------------------------------------------------------
bool SiStripMonitorTrack::clusterInfos(const SiStripCluster* cluster_, const uint32_t& detid, std::string flag, const LocalVector LV)
{
  LogTrace("SiStripMonitorTrack") << "\n["<<__PRETTY_FUNCTION__<<"]" << std::endl;
  //folder_organizer.setDetectorFolder(0);
  if (cluster_==0) return false;
  
  // load cluster info (private members)
  loadCluster(cluster_);
  
  const  edm::ParameterSet ps = conf_.getParameter<edm::ParameterSet>("ClusterConditions");  
  if( ps.getParameter<bool>("On") &&
      (cluster_signal2noise_ < ps.getParameter<double>("minStoN") ||
       cluster_signal2noise_ > ps.getParameter<double>("maxStoN") ||
       cluster_width_ < ps.getParameter<double>("minWidth") ||
       cluster_width_ > ps.getParameter<double>("maxWidth")                    )) return false;
  
  
  // start of the analysis
  
  int SubDet_enum = StripSubdetector(detid).subdetId()-3;
  int iflag =0;
  if      (flag=="OnTrack")  iflag=0;
  else if (flag=="OffTrack") iflag=1;
  NClus[SubDet_enum][iflag]++;
  std::stringstream ss;
  
  LogTrace("SiStripMonitorTrack") << "\n["<<__PRETTY_FUNCTION__<<"]\n" << ss.str() << std::endl;
  
  float cosRZ = -2;
  LogTrace("SiStripMonitorTrack")<< "\n\tLV " << LV.x() << " " << LV.y() << " " << LV.z() << " " << LV.mag() << std::endl;
  if (LV.mag()!=0){
    cosRZ= fabs(LV.z())/LV.mag();
    LogTrace("SiStripMonitorTrack")<< "\n\t cosRZ " << cosRZ << std::endl;
  }
  std::string name;
  
  //Filling SubDet Plots (on Track + off Track)
  std::pair<std::string,int32_t> SubDetAndLayer = folder_organizer.GetSubDetAndLayer(detid);
  name=flag+"_in_"+SubDetAndLayer.first;
  fillTrendMEs(name,cosRZ,flag);
  fillCapacitiveCouplingMEs(name,cosRZ,flag);
  
  char rest[1024];
  int subdetid = ((detid>>25)&0x7);
  if(       subdetid==3 ){
    // ---------------------------  TIB  --------------------------- //
    TIBDetId tib1 = TIBDetId(detid);
    sprintf(rest,"TIB__layer__%d",tib1.layer());
  }else if( subdetid==4){
    // ---------------------------  TID  --------------------------- //
    TIDDetId tid1 = TIDDetId(detid);
    sprintf(rest,"TID__side__%d__wheel__%d",tid1.side(),tid1.wheel());
   }else if( subdetid==5){
     // ---------------------------  TOB  --------------------------- //
     TOBDetId tob1 = TOBDetId(detid);
     sprintf(rest,"TOB__layer__%d",tob1.layer());
   }else if( subdetid==6){
     // ---------------------------  TEC  --------------------------- //
     TECDetId tec1 = TECDetId(detid);
     sprintf(rest,"TEC__side__%d__wheel__%d",tec1.side(),tec1.wheel());
   }else{
     // ---------------------------  ???  --------------------------- //
     edm::LogError("SiStripTkDQM|WrongInput")<<"no such subdetector type :"<<subdetid<<" no folder set!"<<std::endl;
     return 0;
   }
  
  SiStripHistoId hidmanager1;
  
  //Filling Layer Plots
  name= hidmanager1.createHistoLayer("","layer",rest,flag);
  fillTrendMEs(name,cosRZ,flag);
  fillCapacitiveCouplingMEs(name,cosRZ,flag);
  
  //Module plots filled only for onTrack Clusters
  if(Mod_On_){
    if(flag=="OnTrack"){
      SiStripHistoId hidmanager2;
      name =hidmanager2.createHistoId("","det",detid);
      fillModMEs(name,cosRZ); 
    }
  }
  return true;
}

//--------------------------------------------------------------------------------
void SiStripMonitorTrack::fillTrend(MonitorElement* me ,float value)
{
  if(!me) return;
  //check the origin and check options
  int option = conf_.getParameter<edm::ParameterSet>("Trending").getParameter<int32_t>("UpdateMode");
  if(firstEvent==-1) firstEvent = eventNb;
  int CurrentStep = atoi(me->getAxisTitle(1).c_str()+8);
  int firstEventUsed = firstEvent;
  int presentOverflow = (int)me->getBinEntries(me->getNbinsX()+1);
  if(option==2) firstEventUsed += CurrentStep * int(me->getBinEntries(me->getNbinsX()+1));
  else if(option==3) firstEventUsed += CurrentStep * int(me->getBinEntries(me->getNbinsX()+1)) * me->getNbinsX();
  //fill
  me->Fill((eventNb-firstEventUsed)/CurrentStep,value);
  if(eventNb-firstEvent<1) return;
  // check if we reached the end
  if(presentOverflow == me->getBinEntries(me->getNbinsX()+1)) return;
  switch(option) {
  case 1:
    {
      // mode 1: rebin and change X scale
      int NbinsX = me->getNbinsX();
      float entries = 0.;
      float content = 0.;
      float error = 0.;
      int bin = 1;
      int totEntries = int(me->getEntries());
      for(;bin<=NbinsX/2;++bin) {
	content = (me->getBinContent(2*bin-1) + me->getBinContent(2*bin))/2.; 
	error   = pow((me->getBinError(2*bin-1)*me->getBinError(2*bin-1)) + (me->getBinError(2*bin)*me->getBinError(2*bin)),0.5)/2.; 
	entries = me->getBinEntries(2*bin-1) + me->getBinEntries(2*bin);
	me->setBinContent(bin,content*entries);
	me->setBinError(bin,error);
	me->setBinEntries(bin,entries);
      }
      for(;bin<=NbinsX+1;++bin) {
	me->setBinContent(bin,0);
	me->setBinError(bin,0);
	me->setBinEntries(bin,0); 
      }
      me->setEntries(totEntries);
      char buffer[256];
      sprintf(buffer,"EventId/%d",CurrentStep*2);
      me->setAxisTitle(std::string(buffer),1);
      break;
    }
  case 2:
    {
      // mode 2: slide
      int bin=1;
      int NbinsX = me->getNbinsX();
      for(;bin<=NbinsX;++bin) {
	me->setBinContent(bin,me->getBinContent(bin+1)*me->getBinEntries(bin+1));
	me->setBinError(bin,me->getBinError(bin+1));
	me->setBinEntries(bin,me->getBinEntries(bin+1));
      }
      break;
    }
  case 3:
    {
      // mode 3: reset
      int NbinsX = me->getNbinsX();
      for(int bin=0;bin<=NbinsX;++bin) {
	me->setBinContent(bin,0);
	me->setBinError(bin,0);
	me->setBinEntries(bin,0); 
      }
      break;
    }
  }
}

//--------------------------------------------------------------------------------
void SiStripMonitorTrack::fillModMEs(TString name,float cos)
{
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(name);
  if(iModME!=ModMEsMap.end()){
    fillME(iModME->second.ClusterStoN  ,cluster_signal2noise_);
    fillME(iModME->second.ClusterStoNCorr ,cluster_signal2noise_*cos);
    fillME(iModME->second.ClusterCharge,cluster_charge_);
    fillME(iModME->second.ClusterChargeCorr,cluster_charge_*cos);
    fillME(iModME->second.ClusterWidth ,cluster_width_);
    fillME(iModME->second.ClusterPos   ,cluster_position_);
    
    //     std::vector<float> amplitudesL, amplitudesR;
    //     //    amplitudesL = cluster->getRawDigiAmplitudesLR(neighbourStripNumber,*rawDigiHandle,dsv_SiStripCluster,theRawdigiLabel.label()).first;	
    //     //    amplitudesR = cluster->getRawDigiAmplitudesLR(neighbourStripNumber,*rawDigiHandle,dsv_SiStripCluster,theRawdigiLabel.label()).second;
    
    //fill the PGV histo
    
    int PGVposCounter = cluster_firstStrip_ - cluster_maxStrip_;
    for (int i= int(conf_.getParameter<edm::ParameterSet>("TProfileClusterPGV").getParameter<double>("xmin"));i<PGVposCounter;++i)
      fillME(iModME->second.ClusterPGV, i,0.);
    //     for (std::vector<float>::const_iterator it=amplitudesL.begin();it<amplitudesL.end();++it) {
    //       fillME(iModME->second.ClusterPGV, PGVposCounter++,(*it)/cluster_maxCharge_);
    //     }
    for (std::vector<uint16_t>::const_iterator it=cluster_amplitudes_.begin();it<cluster_amplitudes_.end();++it) {
      fillME(iModME->second.ClusterPGV, PGVposCounter++,(*it)/cluster_maxCharge_);
    }
    //     for (std::vector<float>::const_iterator it=amplitudesR.begin();it<amplitudesR.end();++it) {
    //       fillME(iModME->second.ClusterPGV, PGVposCounter++,(*it)/cluster_maxCharge_);
    //     }
    for (int i= PGVposCounter;i<int(conf_.getParameter<edm::ParameterSet>("TProfileClusterPGV").getParameter<double>("xmax"));++i)
      fillME(iModME->second.ClusterPGV, i,0.);
    //end fill the PGV histo
  }
}

//------------------------------------------------------------------------
void SiStripMonitorTrack::fillTrendMEs(std::string name,float cos, std::string flag)
{ 
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(name);
  if(iModME!=ModMEsMap.end()){
    if(flag=="OnTrack"){
      fillME(iModME->second.ClusterStoNCorr,(cluster_signal2noise_)*cos);
      fillTrend(iModME->second.ClusterStoNCorrTrend,(cluster_signal2noise_)*cos);
      fillME(iModME->second.ClusterChargeCorr,cluster_charge_*cos);
      fillTrend(iModME->second.ClusterChargeCorrTrend,cluster_charge_*cos);
    }
    fillME(iModME->second.ClusterStoN  ,cluster_signal2noise_);
    fillTrend(iModME->second.ClusterStoNTrend,cluster_signal2noise_);
    fillME(iModME->second.ClusterCharge,cluster_charge_);
    fillTrend(iModME->second.ClusterChargeTrend,cluster_charge_);
    fillME(iModME->second.ClusterNoise ,cluster_noise_);
    fillTrend(iModME->second.ClusterNoiseTrend,cluster_noise_);
    fillME(iModME->second.ClusterWidth ,cluster_width_);
    fillTrend(iModME->second.ClusterWidthTrend,cluster_width_);
    fillME(iModME->second.ClusterPos   ,cluster_position_);
    
  }
}

//------------------------------------------------------------------------
void SiStripMonitorTrack::fillCapacitiveCouplingMEs(std::string name, float cos, std::string flag) {
  std::map<TString, ModMEs>::iterator iModME  = ModMEsMap.find(name);
  if(iModME!=ModMEsMap.end()){
    // Capacitive Coupling analysis
    if( cos > 0.9 ) { // perpendicular track
      LogTrace("SiStripMonitorTrack") << "\t\t Perpendicular Track" << std::endl;
      
      // calculate symmetric eta function
      float symmetricEta = ( !dsv_SiStripRawDigi.failedToGet()
			     ?
			     SymEta(cluster_chargeRawCentral_,cluster_chargeRawLeft_,cluster_chargeRawRight_)
			     :
			     SymEta(cluster_maxCharge_,cluster_chargeLeft_,cluster_chargeRight_)
			     );
      
      // Summary
      LogTrace("SiStripMonitorTrack")    
	<<"\n\t\t Cluster with multiplicity " << cluster_width_
	<< " in det " << cluster_detid_
	<< ": Eta Function from Charge"
	<<"\n\t\t\t Signal = " << cluster_charge_
	<<"\n\t\t\t Noise  = " << cluster_noise_
	<<"\n\t\t\t S/N    = " << cluster_signal2noise_
	<<"\n\t\t\t  Digi Central C = " << cluster_chargeRawCentral_
	<<"\n\t\t\t  Digi    Left L = " << cluster_chargeRawLeft_
	<<"\n\t\t\t  Digi   Right R = " << cluster_chargeRawRight_
	<<"\n\t\t\t  Cluster Left L = " << cluster_chargeLeft_
	<<"\n\t\t\t Cluster Right R = " << cluster_chargeRight_
	<<"\n\t\t\t     Max = " << cluster_maxCharge_
	<<"\n\t\t\t     Pos = " << cluster_maxStrip_
	<<"\n\t\t\t Symmetric Eta = " << symmetricEta
	<< std::endl;
      for (std::vector<uint16_t>::const_iterator it=cluster_amplitudes_.begin();it<cluster_amplitudes_.end();++it) {
	LogTrace("SiStripMonitorTrack")    
	  <<"\t\t\t       " << (short)(*it) << std::endl;
      }
      //
      
      // fill monitor elements
      fillME(iModME->second.ClusterSymmEtaCC , symmetricEta);
      fillTrend(iModME->second.ClusterSymmEtaCCTrend, symmetricEta);
      //
      
    } // perpendicular track
    
  }
}

//------------------------------------------------------------------------
void SiStripMonitorTrack::loadCluster(const SiStripCluster* cluster) {
  cluster_charge_         = 0;
  cluster_noise_          = 0;
  cluster_signal2noise_   = 0;
  cluster_width_          = 0;
  cluster_position_       = 0;
  cluster_detid_          = 0;
  cluster_maxCharge_      = 0;
  cluster_maxStrip_       = 0;
  cluster_firstStrip_     = 0;
  cluster_chargeLeft_     = 0;
  cluster_chargeRight_    = 0;
  cluster_chargeRawLeft_  = 0;
  cluster_chargeRawRight_ = 0;
  cluster_amplitudes_.clear();
  
  //
  cluster_position_   = cluster->barycenter();        // get the position of the cluster
  cluster_detid_      = cluster->geographicalId();    // get the detector id
  cluster_amplitudes_ = cluster->amplitudes();        // get the array of strips charges
  cluster_firstStrip_ = cluster->firstStrip();        // get the first strip belonging to the cluster
  cluster_width_      = cluster->amplitudes().size(); // get the cluster width

  int numberOfPosAmplitudes = 0;
  float       clusterNoise2 = 0;
  SiStripNoises::Range detNoiseRange = noiseHandle_->getRange(cluster_detid_);
  
  for(size_t i=0; i<cluster_amplitudes_.size(); i++){
    float noise=noiseHandle_->getNoise(cluster_firstStrip_+i,detNoiseRange);
    if(cluster_amplitudes_[i]>0){
      cluster_charge_+=cluster_amplitudes_[i]; // get the charge of the cluster
      clusterNoise2+=noise*noise;
      numberOfPosAmplitudes++;
    }
  }
  cluster_noise_        = sqrt(clusterNoise2/numberOfPosAmplitudes);// get the noise of the cluster
  cluster_signal2noise_ = cluster_charge_ / cluster_noise_;         // get the signal to noise ratio (used several times)
  
  
  for(size_t i=0; i< cluster_amplitudes_.size();i++){
    if (cluster_amplitudes_[i] > 0){
      if (cluster_maxCharge_<cluster_amplitudes_[i]){ 
	cluster_maxCharge_ = cluster_amplitudes_[i]; // get the value of the charge of the strip with maximum charge
	cluster_maxStrip_  = i;
      }
    } 
  } 
  cluster_maxStrip_+=cluster_firstStrip_; // get the strip with maximum charge
  
  for(size_t i=0; i < cluster_amplitudes_.size();i++){
    if(i+cluster_firstStrip_ < cluster_maxStrip_) cluster_chargeLeft_  += cluster_amplitudes_[i]; // get the charge Left of the cluster max strip
    if(i+cluster_firstStrip_ > cluster_maxStrip_) cluster_chargeRight_ += cluster_amplitudes_[i]; // get the charge Right of the cluster max strip
  }


  // calculate charge Left and Right charge of the cluster from RawDigis...
  if( ! dsv_SiStripRawDigi.failedToGet() ){ // ...only if RawDigis exist
    // take the RawDigis of the detector...
    if((*(dsv_SiStripRawDigi.product())).find(cluster_detid_) != (*(dsv_SiStripRawDigi.product())).end() ) { // ...if they exist
      const edm::DetSet<SiStripRawDigi> ds_SiStripRawDigi = (*(dsv_SiStripRawDigi.product()))[cluster_detid_];
      LogDebug("SiStripMonitorTrack") << "RawDigis found " << ds_SiStripRawDigi.size() << std::endl;
      // Central charge from RawDigi
      //    cluster_chargeRawCentral_ = rawCharges[cluster_maxStrip_];
      edm::DetSet<SiStripRawDigi>::const_iterator digis_iter = ds_SiStripRawDigi.data.begin() + cluster_maxStrip_;
      cluster_chargeRawCentral_ = digis_iter->adc();
      // Left and Right charge from RawDigi
      for(unsigned int iNear = 1; iNear<=neighbourStripNumber; iNear++){ // neighbour channels
	if( (cluster_maxStrip_ - iNear) >= 0 ) {
	  digis_iter = ds_SiStripRawDigi.data.begin() + cluster_maxStrip_ - iNear;
	  cluster_chargeRawLeft_  += digis_iter->adc(); // get the raw charge Left of the cluster max strip
	}
	if( (cluster_maxStrip_ + iNear) < ds_SiStripRawDigi.size() ) {
	  digis_iter = ds_SiStripRawDigi.data.begin() + cluster_maxStrip_ + iNear;
	  cluster_chargeRawRight_ += digis_iter->adc(); // get the raw charge Right of the cluster max strip
	}
      } // neighbour channels
    } else { // no SiStripRawDigi in detid
      LogDebug("SiStripMonitorTrack") << "RawDigis not found in detid " << cluster_detid_ << std::endl;
    }
  } else { // no SiStripRawDigi at all
    LogDebug("SiStripMonitorTrack") << "RawDigis collection not found" << std::endl;
  }
  
}

//------------------------------------------------------------------------
// Symmetric Eta function
float SiStripMonitorTrack::SymEta( float clusterCentralCharge, float clusterLeftCharge, float clusterRightCharge){
  float symeta = ( clusterLeftCharge + clusterRightCharge )/( 2 * clusterCentralCharge );
  return symeta;
}

DEFINE_FWK_MODULE(SiStripMonitorTrack);
