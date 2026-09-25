#include "def.h"
#include "func.h"
#include "EMV.h"
#include "EmvCommon.h"

#include ".\emvctls\PayPass.h"
#include ".\emvctls\PayWave.h"
#include ".\emvctls\qUICS.h"
#include ".\emvctls\Rupay.h"
#include ".\xgdlib.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>

POS_COM PosCom;

char g_EmvFullOrSim = 0;

static int IccOnline = 0;
static int PWaveCLAllowed;
static int QuicsCLAllowed;

void AddCapkExample()
{
	int ret;
	EMV_CAPK tempCAPK;

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;
	memcpy(tempCAPK.ExpDate, "\x25\x12\x31", 3);	
	AscToBcd_Api(tempCAPK.RID, "A000000333", 10);
	tempCAPK.KeyID    = 0x03;
	tempCAPK.ModulLen = 352 / 2;
	AscToBcd_Api(tempCAPK.Modul, "B0627DEE87864F9C18C13B9A1F025448BF13C58380C91F4CEBA9F9BCB214FF8414E9B59D6ABA10F941C7331768F47B2127907D857FA39AAF8CE02045DD01619D689EE731C551159BE7EB2D51A372FF56B556E5CB2FDE36E23073A44CA215D6C26CA68847B388E39520E0026E62294B557D6470440CA0AEFC9438C923AEC9B2098D6D3A1AF5E8B1DE36F4B53040109D89B77CAFAF70C26C601ABDF59EEC0FDC8A99089140CD2E817E335175B03B7AA33D", 352);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x03) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 0x01;
	tempCAPK.ArithInd = 0x01;
	AscToBcd_Api(tempCAPK.ExpDate, "351231", 6);
	AscToBcd_Api(tempCAPK.RID, "A000000004", 10);
	tempCAPK.KeyID    = 0x05;
	tempCAPK.ModulLen = 352 / 2;
	AscToBcd_Api(tempCAPK.Modul, "B8048ABC30C90D976336543E3FD7091C8FE4800DF820ED55E7E94813ED00555B573FECA3D84AF6131A651D66CFF4284FB13B635EDD0EE40176D8BF04B7FD1C7BACF9AC7327DFAA8AA72D10DB3B8E70B2DDD811CB4196525EA386ACC33C0D9D4575916469C4E4F53E8E1C912CC618CB22DDE7C3568E90022E6BBA770202E4522A2DD623D180E215BD1D1507FE3DC90CA310D27B3EFCCD8F83DE3052CAD1E48938C68D095AAC91B5F37E28BB49EC7ED597", 352);
	tempCAPK.ExponentLen = 0x01;
	AscToBcd_Api(tempCAPK.Exponent, "030000", 6);
	AscToBcd_Api(tempCAPK.CheckSum, "EBFA0D5D06D8CE702DA3EAE890701D45E274C845", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x05) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 0x01;
	tempCAPK.ArithInd = 0x01;
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x50;
	tempCAPK.ModulLen = 256 / 2;
	AscToBcd_Api(tempCAPK.Modul, "D11197590057B84196C2F4D11A8F3C05408F422A35D702F90106EA5B019BB28AE607AA9CDEBCD0D81A38D48C7EBB0062D287369EC0C42124246AC30D80CD602AB7238D51084DED4698162C59D25EAC1E66255B4DB2352526EF0982C3B8AD3D1CCE85B01DB5788E75E09F44BE7361366DEF9D1E1317B05E5D0FF5290F88A0DB47", 256);
	tempCAPK.ExponentLen = 3;		
	AscToBcd_Api(tempCAPK.Exponent, "010001", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "B769775668CACB5D22A647D1D993141EDAB7237B", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x50) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x51;
	tempCAPK.ModulLen = 288 / 2;
	AscToBcd_Api(tempCAPK.Modul, "DB5FA29D1FDA8C1634B04DCCFF148ABEE63C772035C79851D3512107586E02A917F7C7E885E7C4A7D529710A145334CE67DC412CB1597B77AA2543B98D19CF2CB80C522BDBEA0F1B113FA2C86216C8C610A2D58F29CF3355CEB1BD3EF410D1EDD1F7AE0F16897979DE28C6EF293E0A19282BD1D793F1331523FC71A228800468C01A3653D14C6B4851A5C029478E757F", 288);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "B9D248075A3F23B522FE45573E04374DC4995D71", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x51) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x53;
	tempCAPK.ModulLen = 496 / 2;
	AscToBcd_Api(tempCAPK.Modul, "BCD83721BE52CCCC4B6457321F22A7DC769F54EB8025913BE804D9EABBFA19B3D7C5D3CA658D768CAF57067EEC83C7E6E9F81D0586703ED9DDDADD20675D63424980B10EB364E81EB37DB40ED100344C928886FF4CCC37203EE6106D5B59D1AC102E2CD2D7AC17F4D96C398E5FD993ECB4FFDF79B17547FF9FA2AA8EEFD6CBDA124CBB17A0F8528146387135E226B005A474B9062FF264D2FF8EFA36814AA2950065B1B04C0A1AE9B2F69D4A4AA979D6CE95FEE9485ED0A03AEE9BD953E81CFD1EF6E814DFD3C2CE37AEFA38C1F9877371E91D6A5EB59FDEDF75D3325FA3CA66CDFBA0E57146CC789818FF06BE5FCC50ABD362AE4B80996D", 496);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "AC213A2E0D2C0CA35AD0201323536D58097E4E57", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x53) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x96;
	tempCAPK.ModulLen = 256 / 2;
	AscToBcd_Api(tempCAPK.Modul, "B74586D19A207BE6627C5B0AAFBC44A2ECF5A2942D3A26CE19C4FFAEEE920521868922E893E7838225A3947A2614796FB2C0628CE8C11E3825A56D3B1BBAEF783A5C6A81F36F8625395126FA983C5216D3166D48ACDE8A431212FF763A7F79D9EDB7FED76B485DE45BEB829A3D4730848A366D3324C3027032FF8D16A1E44D8D", 256);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "7616E9AC8BE014AF88CA11A8FB17967B7394030E", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x96) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x58;
	tempCAPK.ModulLen = 400 / 2;
	AscToBcd_Api(tempCAPK.Modul, "99552C4A1ECD68A0260157FC4151B5992837445D3FC57365CA5692C87BE358CDCDF2C92FB6837522842A48EB11CDFFE2FD91770C7221E4AF6207C2DE4004C7DEE1B6276DC62D52A87D2CD01FBF2DC4065DB52824D2A2167A06D19E6A0F781071CDB2DD314CB94441D8DC0E936317B77BF06F5177F6C5ABA3A3BC6AA30209C97260B7A1AD3A192C9B8CD1D153570AFCC87C3CD681D13E997FE33B3963A0A1C79772ACF991033E1B8397AD0341500E48A24770BC4CBE19D2CCF419504FDBF0389BC2F2FDCD4D44E61F", 400);
	tempCAPK.ExponentLen = 3;		
	AscToBcd_Api(tempCAPK.Exponent, "010001", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "753ED0AA23E4CD5ABD69EAE7904B684A34A57C22", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x58) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x52;
	tempCAPK.ModulLen = 352 / 2;
	AscToBcd_Api(tempCAPK.Modul, "AFF740F8DBE763F333A1013A43722055C8E22F41779E219B0E1C409D60AFD45C8789C57EECD71EA4A269A675916CC1C5E1A05A35BD745A79F94555CE29612AC9338769665B87C3CA8E1AC4957F9F61FA7BFFE4E17631E937837CABF43DD6183D6360A228A3EBC73A1D1CDC72BF09953C81203AB7E492148E4CB774CDDFAAC3544D0DD4F8C8A0E9C70B877EA79F2C22E4CE52C69F3EF376F61B0F43A540FE96C63F586310C3B6E39C78C4D647CADB5933", 352);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "42D96E6E1217E5B59CC2079CE50C3D9F55B6FC1D", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x52) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000003", 10);
	tempCAPK.KeyID    = 0x57;
	tempCAPK.ModulLen = 192 / 2;
	AscToBcd_Api(tempCAPK.Modul, "942B7F2BA5EA307312B63DF77C5243618ACC2002BD7ECB74D821FE7BDC78BF28F49F74190AD9B23B9713B140FFEC1FB429D93F56BDC7ADE4AC075D75532C1E590B21874C7952F29B8C0F0C1CE3AEEDC8DA25343123E71DCF86C6998E15F756E3", 192);
	tempCAPK.ExponentLen = 3;		
	AscToBcd_Api(tempCAPK.Exponent, "010001", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "251A5F5DE61CF28B5C6E2B5807C0644A01D46FF5", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x57) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000004", 10);
	tempCAPK.KeyID    = 0xF5;
	tempCAPK.ModulLen = 496 / 2;
	AscToBcd_Api(tempCAPK.Modul, "A6E6FB72179506F860CCCA8C27F99CECD94C7D4F3191D303BBEE37481C7AA15F233BA755E9E4376345A9A67E7994BDC1C680BB3522D8C93EB0CCC91AD31AD450DA30D337662D19AC03E2B4EF5F6EC18282D491E19767D7B24542DFDEFF6F62185503532069BBB369E3BB9FB19AC6F1C30B97D249EEE764E0BAC97F25C873D973953E5153A42064BBFABFD06A4BB486860BF6637406C9FC36813A4A75F75C31CCA9F69F8DE59ADECEF6BDE7E07800FCBE035D3176AF8473E23E9AA3DFEE221196D1148302677C720CFE2544A03DB553E7F1B8427BA1CC72B0F29B12DFEF4C081D076D353E71880AADFF386352AF0AB7B28ED49E1E672D11F9", 496);
	tempCAPK.ExponentLen = 3;		
	AscToBcd_Api(tempCAPK.Exponent, "010001", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "C2239804C8098170BE52D6D5D4159E81CE8466BF", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0xf5) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "991231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000658", 10);
	tempCAPK.KeyID    = 0xD1;
	tempCAPK.ModulLen = 240;
	AscToBcd_Api(tempCAPK.Modul, "A3547415A7D237C09FC8AFF989FDA49E5B3275545026361C1A8DE477467F963D8F6F58A2F16E0885E4759CA58F72A5B5446CE3893155EFD978B2F0D8D1A7294AC7870D65B5CC78286F96237EFCBA02C6844A84DB79A01D225FF3BEAB3761AFC52AEDD57764483C980076D10E4C3485011DD93A970C57FC72A1CCA47C7D1B57E5D7798A180BF08455A4D602CFC3C881034B52D6DF2C3B1A8FEE7E6539EA35F6B5C123A822AA73FB6BDFD894AEB8381A62413EFB030F85DC45D71B66A322F1532A91C9AD8E4820AC18C544A623FC3E401D42498C1C9B88E5A6B7DA2D9E0BF7CB3F921242B5352302B95EE1344D79ECE49D", 240*2);
	tempCAPK.ExponentLen = 3;		
	AscToBcd_Api(tempCAPK.Exponent, "010001", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "05567628480B757FE633999C9AE1D9F420F84EE3", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0xd1) failed(%d) !!!", ret);

	memset(&tempCAPK, 0, sizeof(EMV_CAPK));
	tempCAPK.HashInd  = 1;
	tempCAPK.ArithInd = 1;	
	AscToBcd_Api(tempCAPK.ExpDate, "251231", 6);	
	AscToBcd_Api(tempCAPK.RID, "A000000065", 10);
	tempCAPK.KeyID    = 0x09;
	tempCAPK.ModulLen = 256 / 2;
	AscToBcd_Api(tempCAPK.Modul, "A0BA1E941BA2B11DFB9AC5139041CC58B870A3B328F4712DD844439E6544469FD31106167FE926583CBCED6D573DECF9AF67D09875AF285C189681D4045883031E99A0A0F456DD31857DC58960EC24689F68FECEF88832B389D66D2A0481B14B0E05FD36CC00163FCAABAE73B5273D5F1206D4E246DC8AA1977A685FDD344B0D", 256);
	tempCAPK.ExponentLen = 1;		
	AscToBcd_Api(tempCAPK.Exponent, "03", tempCAPK.ExponentLen * 2);
	AscToBcd_Api(tempCAPK.CheckSum, "0000000000000000000000000000000111111111", 40);

	ret = Common_AddCapk_Api(&tempCAPK);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_AddCapk_Api(0x09) failed(%d) !!!", ret);
}

void PayPassAddAppExp(int transType)
{
	PAYPASS_APPLIST app;

	memset(&app, 0, sizeof(PAYPASS_APPLIST));
	AscToBcd_Api(app.AID, "A0000000041010", 14);
	app.AidLen = 7;
	AscToBcd_Api(app.Version, "0002", 4);
	app.CVMCapabilityCVM = 0x60;
	app.CVMCapabilityNoCVM = 0x08;
	AscToBcd_Api(app.uDOL, "039F6A04", 6);
	app.KernelID = 0x02;
	AscToBcd_Api(app.MagStripeAVN, "0001", 4);
	AscToBcd_Api(app.RiskManData, "086CFF000000000000", 18);
	if (transType == 0x17)
		app.MagCVMCapabilityCVM = 0x20;
	else
		app.MagCVMCapabilityCVM = 0x10;

	app.MagCVMCapabilityNoCVM = 0x00;
	app.FloorLimit = 10000;
	app.TransLimitNoODCVM = 500000;
	app.TransLimitODCVM   = 500000;

	memset(app.TACDenial, 0, 6);
	memset(app.TACOnline, 0, 6);
	memset(app.TACDefault, 0, 6);
	app.KernelConfig = 0x20;

	if ((transType == 0x00) || (transType == 0x12) || (transType == 0x21))
		app.CVMLimit = 1000;
	else
		app.CVMLimit = 20000;

	PayPass_AddApp_Api(&app);

	AscToBcd_Api(app.AID, "A0000000043060", 14);
	app.AidLen = 7;
	app.KernelConfig = 0xA0;
	app.CVMLimit = 30000;
	AscToBcd_Api(app.RiskManData, "0844FF800000000000", 18);

	if (transType == 0x17) {
		app.MagCVMCapabilityCVM = 0xF0;
		app.MagCVMCapabilityNoCVM = 0xF0;
	}
	else {
		app.MagCVMCapabilityCVM = 0x10;
		app.MagCVMCapabilityNoCVM = 0x00;
	}

	PayPass_AddApp_Api(&app);
}

void PayWaveAddAppExp()
{
	PAYWAVE_APPLIST item;

	memset(&item, 0, sizeof(item));
	item.SelFlag = 0;
	item.bStatusCheck = 1;
	item.bZeroAmtCheck = 1;
	item.ZeroAmtCheckOpt = 1;
	item.bTransLimitCheck = 1;
	item.bCVMLimitCheck = 1;
	item.bFloorLimitCheck = 1;
	item.bHasFloorLimit = 1;
	item.TransLimit = 500000;
	item.CVMLimit = 2;
	item.FloorLimit = 1000;
	item.TermFloorLimit = 1000;

	memset(item.AID, 0, sizeof(item.AID));
	AscToBcd_Api(item.AID, "A0000000031010", 7*2 ); 
	item.AidLen= 7;
	PayWave_AddApp_Api(&item);

	memset(item.AID, 0, sizeof(item.AID));
	AscToBcd_Api(item.AID, "A000000003", 5*2); 
	item.AidLen= 5;
	PayWave_AddApp_Api(&item);

	memset(item.AID, 0, sizeof(item.AID));
	AscToBcd_Api(item.AID, "A00000000310", 6*2); 
	item.AidLen= 6;
	PayWave_AddApp_Api(&item);

	memset(item.AID, 0, sizeof(item.AID));
	AscToBcd_Api(item.AID, "A000000003101002", 8*2); 
	item.AidLen= 8;    	 	
	PayWave_AddApp_Api(&item);
}

void QuicsAddAppExp()
{
	QUICS_APPLIST item;

	memset(&item, 0, sizeof(item));
	item.SelFlag = 0;
	item.bStatusCheck = 1;
	item.bHasFloorLimit = 1;
	item.TransLimit = 500000;
	item.CVMLimit = 2;
	item.FloorLimit = 1000;
	item.TermFloorLimit = 1000;

	memset(item.AID, 0, sizeof(item.AID));
	AscToBcd_Api(item.AID, "A000000333", 5*2 );
	item.AidLen= 5;
	qUICS_AddApp_Api(&item);
}

void EmvAddAppExp()
{
	int ret;

	EMV_APPLIST emv_applist;
	memset(&emv_applist, 0, sizeof(emv_applist));

	EMV_ClearApp_Api();

	// 1
	AscToBcd_Api(emv_applist.Version, "008C", 			  4);
	AscToBcd_Api(emv_applist.AID,     "A000000333010101", 8 * 2);
	emv_applist.AidLen = 8;

	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(333010101) failed(%d) !!!", ret);

	// 2
	AscToBcd_Api(emv_applist.Version, "008C", 		    2 * 2);
	AscToBcd_Api(emv_applist.AID,     "A0000003330101", 7 * 2);
	emv_applist.AidLen = 7;

	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3330101) failed(%d) !!!", ret);

	// 3
	AscToBcd_Api(emv_applist.AID, 	  "A0000000031010", 7 * 2);
	AscToBcd_Api(emv_applist.Version, "0096", 			2 * 2);
	emv_applist.AidLen = 7;

	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(31010) failed(%d) !!!", ret);

	// 4
	AscToBcd_Api(emv_applist.AID, "A000000003101003", 8 * 2);
	emv_applist.AidLen = 8;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3101003) failed(%d) !!!", ret);

	// 5
	AscToBcd_Api(emv_applist.AID, "A000000003101004", 8 * 2);
	emv_applist.AidLen = 8;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3101004) failed(%d) !!!", ret);

	// 6
	AscToBcd_Api(emv_applist.AID, "A000000003101005", 8 * 2);
	emv_applist.AidLen = 8;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3101005) failed(%d) !!!", ret);

	// 7
	AscToBcd_Api(emv_applist.AID, "A000000003101006", 8 * 2);
	emv_applist.AidLen = 8;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3101006) failed(%d) !!!", ret);

	// 8
	AscToBcd_Api(emv_applist.AID, "A000000003101007", 8 * 2);
	emv_applist.AidLen = 8;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(3101007) failed(%d) !!!", ret);

	// 9
	AscToBcd_Api(emv_applist.AID, "A0000000999090", 7 * 2);
	emv_applist.AidLen = 7;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(999090) failed(%d) !!!", ret);

	// 10
	AscToBcd_Api(emv_applist.AID, "A00000999901", 6 * 2);
	emv_applist.AidLen = 6;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(999901) failed(%d) !!!", ret);

	// 11
	AscToBcd_Api(emv_applist.AID, "A0000000041010", 7 * 2);
	emv_applist.AidLen = 7;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(41010) failed(%d) !!!", ret);

	// 12
	AscToBcd_Api(emv_applist.AID, "A0000000651010", 7 * 2);
	emv_applist.AidLen = 7;
	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(651010) failed(%d) !!!", ret);

	// 13
	AscToBcd_Api(emv_applist.Version, "0096",       2 * 2);
	AscToBcd_Api(emv_applist.AID, 	  "A122334455", 5 * 2);
	emv_applist.AidLen = 5;
	emv_applist.SelFlag = PART_MATCH;

	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(334455) failed(%d) !!!", ret);

	// 14
	AscToBcd_Api(emv_applist.Version, "0096", 	    2);
	AscToBcd_Api(emv_applist.AID,     "A000000003", 5);
	emv_applist.AidLen = 5;

	ret = EMV_AddApp_Api(&emv_applist);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_AddApp_Api(000003) failed(%d) !!!", ret);
}

int App_CommonSelKernel()
{
	LogPrintNoRet("=> [EmvCommon.c]->App_CommonSelKernel()");

	int ret;
	COMMON_PPSE_STATUS ppse;

	ret = Common_SelectPPSE_Api(&ppse);
	LogPrintWithRet(0, "=> [EmvCommon.c]->Common_SelectPPSE_Api(): ", ret);

	if (ret != EMV_OK)
		return ret;

	//paypass or not ?
	ret = PayPass_SelectApp_Api(&ppse);
	LogPrintWithRet(0, "=> [EmvCommon.c]->PayPass_SelectApp_Api(): ", ret);

	if(ret == ERR_SELECTNEXT)
	{
		while(1)
		{
			ret = PayPass_SelectApp_Api(NULL);
			LogPrintWithRet(0, "=> [EmvCommon.c]->PayPass_SelectApp_Api(NULL): ", ret);

			if (ret == ERR_SELECTNEXT)
				continue;
			else
				break;
		}
	}
	if (ret == EMV_OK)
		return TYPE_KER_PAYPASS;

	if(PWaveCLAllowed == 1)
	{
		//paywave or not ?
		ret = PayWave_SelectApp_Api(&ppse);
		LogPrintWithRet(0, "=> [EmvCommon.c]->PayWave_SelectApp_Api(): ", ret);

		if(ret == ERR_SELECTNEXT)
		{
			while(1)
			{
				ret = PayWave_SelectApp_Api(NULL);
				LogPrintWithRet(0, "=> [EmvCommon.c]->PayWave_SelectApp_Api(NULL): ", ret);

				if (ret == ERR_SELECTNEXT)
					continue;
				else
					break;
			}
		}

		if (ret == EMV_OK)
			return TYPE_KER_PAYWAVE;
	}
	
	if(QuicsCLAllowed == 1)
	{
		//quics or not ?
		ret = qUICS_SelectApp_Api(&ppse, PosCom.stTrans.lTraceNo);
		LogPrintWithRet(0, "=> [EmvCommon.c]->qUICS_SelectApp_Api(): ", ret);

		if(ret == ERR_SELECTNEXT)
		{
			while(1)
			{
				ret = qUICS_SelectApp_Api(NULL, PosCom.stTrans.lTraceNo);
				LogPrintWithRet(0, "=> [EmvCommon.c]->qUICS_SelectApp_Api(NULL): ", ret);

				if (ret == ERR_SELECTNEXT)
					continue;
				else
					break;
			}
		}

		if (ret == EMV_OK)
			return TYPE_KER_QUICS;
	}

	ret = RUPAY_SelectApp_Api(PosCom.stTrans.lTraceNo, &ppse);
	LogPrintWithRet(0, "=> [EmvCommon.c]->RUPAY_SelectApp_Api(): ", ret);

	if(ret == ERR_SELECTNEXT)
	{
		while(1)
		{
			ret = RUPAY_SelectApp_Api(PosCom.stTrans.lTraceNo, NULL);
			LogPrintWithRet(0, "=> [EmvCommon.c]->RUPAY_SelectApp_Api(NULL): ", ret);

			if (ret == ERR_SELECTNEXT)
				continue;
			else
				break;
		}
	}
	if (ret == EMV_OK)
		return TYPE_KER_RUPAY;

	return TYPE_KER_ERR;	  //not paypass/paywave/mir/jspeedy
}

int App_RupayTrans()
{
	int ret = 0;
	int cvm = 0, path = 0;
	int CanRemoveCard = 0;

	ret = RUPAY_InitApp_Api();
	if (ret != EMV_OK)
		return ret;

	ret = RUPAY_ReadAppData_Api();
	if (ret != EMV_OK)
		return ret;

	ret = RUPAY_OfflineDataAuth_Api();
	if (ret != EMV_OK)
		return ret;

	ret = RUPAY_ProcRestrictions_Api();
	if (ret != EMV_OK)
		return ret;


	ret = RUPAY_VerifyCardholder_Api(&cvm);
	if(ret != 0)
		return ret;

	ret = RUPAY_RiskManagement_Api();
	if (ret != EMV_OK)
		return ret;

	PosCom.stTrans.IccOnline = 0;
	ret = RUPAY_TermActAnalyse_Api(&PosCom.stTrans.IccOnline, &CanRemoveCard);
	if (ret != EMV_OK)
		return ret;

	if(CanRemoveCard == 1)
	{
		//can remove card
	}

	if (cvm == QUICS_CVM_ONLINE_PIN)
	{
		ret = EnterPIN(0);
		if (ret != 0)
			return ret;
	}
	else if (cvm == QUICS_CVM_SIGNATURE)
	{
	}

	if(PosCom.stTrans.IccOnline != 1) // non-online process
		return ret;

	//connect to server, send request, receive response data from server, and call RUPAY_chkIf2ndTap_Api / RUPAY_Complete_Api

	return ret;
}

int App_QuicsTrans()
{
	int ret = 0;
	int cvm = 0, path = 0;

	while (1) {
		if (ret == EMV_OK)
			ret = qUICS_InitApp_Api(&path);

		if (ret == ERR_SELECTNEXT) {
			ret = qUICS_SelectApp_Api(NULL, PosCom.stTrans.lTraceNo);
			continue;
		}

		break;
	}

	if (ret != EMV_OK)
		return ret;

	ret = qUICS_ReadAppData_Api();
	if (ret != EMV_OK)
		return ret;

	//We can remove card here.

	ret = qUICS_OfflineDataAuth_Api();
	if (ret != EMV_OK)
		return ret;

	PosCom.stTrans.IccOnline = 0;
	ret = qUICS_VerifyCardholder_Api(&cvm, &PosCom.stTrans.IccOnline);
	if(ret != 0)
		return ret;

	if (cvm == QUICS_CVM_ONLINE_PIN)
	{
		ret = EnterPIN(0);
		if (ret != 0)
			return ret;
	}
	else if (cvm == QUICS_CVM_SIGNATURE)
	{
	}

	if(PosCom.stTrans.IccOnline)
	{
		//connect to server, send request, receive response data from server
	}

	return ret;
}

int App_PaypassTrans()
{
	int ret = 0, onlineResult = 0, path = 0, cvm = 0, canRemoveCard = 0, needOnline = 0;

	//PAYPASS_OUTCOME  outcome;

SELECT_NEXT:
	if (ret != EMV_OK) goto TRANS_COMPLETED;

	ret = PayPass_InitApp_Api(&path);
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayPass_InitApp_Api() failed(%d) !!!", ret);
		goto TRANS_COMPLETED;
	}

	ret = PayPass_ReadAppData_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayPass_ReadAppData_Api() failed(%d) !!!", ret);
		goto TRANS_COMPLETED;
	}

	GetPanNumber();

	LogPrintWithRet(0, "path = ", path);

	if (path == PAYPASS_PATH_MAGSTRIPE) { // 2
		ret = PayPass_ProcMSTrans_Api();
		if (ret != EMV_OK) {
			LogPrintWithRet(1, "!!! PayPass_ProcMSTrans_Api() failed(%d) !!!", ret);
			goto TRANS_COMPLETED;
		}

		canRemoveCard = 1;
	}
	else {
		ret = PayPass_ProcRestrictions_Api();
		if (ret != EMV_OK) {
			LogPrintWithRet(1, "!!! PayPass_ProcRestrictions_Api() failed(%d) !!!", ret);
			goto TRANS_COMPLETED;
		}

		ret = PayPass_VerifyCardholder_Api(&cvm);
		if (ret != EMV_OK) {
			LogPrintWithRet(1, "!!! PayPass_VerifyCardholder_Api() failed(%d) !!!", ret);
			goto TRANS_COMPLETED;
		}

		ret = PayPass_TermActAnalyse_Api();
		if (ret != EMV_OK) {
			LogPrintWithRet(1, "!!! PayPass_TermActAnalyse_Api() failed(%d) !!!", ret);
			goto TRANS_COMPLETED;
		}

		ret = PayPass_CardActAnalyse_Api(&canRemoveCard);
		if (ret != EMV_OK) {
			LogPrintWithRet(1, "!!! PayPass_CardActAnalyse_Api() failed(%d) !!!", ret);
			goto TRANS_COMPLETED;
		}
	}

	LogPrintWithRet(0, "canRemoveCard = ", canRemoveCard);

	if (canRemoveCard == 1) {
		ScrDisp_Api(LINE3, 0, "Please remove card", FDISP|CDISP);
	}

	if (path == PAYPASS_PATH_MAGSTRIPE) // 2
	{
		ret = PayPass_CompleteMSTrans_Api(&cvm);
		LogPrintWithRet(0, "PayPass_CompleteMSTrans_Api(): ", ret);
	}
	else {
		ret = PayPass_CompleteTrans_Api(&needOnline);  //online or not
		LogPrintWithRet(0, "PayPass_CompleteTrans_Api(): ", ret);

		IccOnline = needOnline;
	}   

	if (ret != EMV_OK) goto TRANS_COMPLETED;

	LogPrintWithRet(0, "cvm = ",        cvm);
	LogPrintWithRet(0, "needOnline = ", needOnline);

	if (cvm == PAYPASS_CVM_SIGNATURE) { // 0x01
		// todo for: Signature
	}
	else if (cvm == PAYPASS_CVM_ONLINE_PIN) { // 0x02
		ret = EnterPIN(0);
		if (ret != 0) goto TRANS_COMPLETED;
	}

	onlineResult = ONLINE_APPROVE;
	if (IccOnline) {
		//build the message from variable outcome and sent to server if necessary
		//PayPass_GetOutcome_Api(&outcome);

		//onlineResult = online process
		if (onlineResult == ONLINE_FAILED) {
			//reversal 
		}
	}

TRANS_COMPLETED:
	if (ret == ERR_SELECTNEXT) { // -114
		ret = PayPass_SelectApp_Api(NULL);
		LogPrintWithRet(0, "PayPass_SelectApp_Api(NULL): ", ret);
		goto SELECT_NEXT;
	}

	return ret;
}

int App_PaywaveTrans()
{ 
	u8 buf[1024];
	int ret = 0, onlineResult, len, path = 0, cvm = 0, needOnline = 0, needIssuer = 0, authCodeLen = 0, scriptLen = 0;

	COMMON_TERMINAL_PARAM param;
	if(isSmallMainScreen())
	{
		ScrClrLine_Api(LINE1, LINE2);
		ScrDisp_Api(LINE2, 0, "Processing...", CDISP);
	}
	else
	{
		ScrClrLine_Api(LINE2, LINE5);
		ScrDisp_Api(LINE3, 0, "Processing...", CDISP);
		ScrDisp_Api(LINE4, 0, "Please Wait", CDISP);
	}

WAVE_SELECT_NEXT:
	if (ret != EMV_OK) goto WAVE_COMPLETED;

	ret = PayWave_InitApp_Api(&path);
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayWave_InitApp_Api() failed(%d) !!!", ret);
		goto WAVE_COMPLETED;
	}

	LogPrintWithRet(0, "path = ", path);

	if (path == PAYWAVE_PATH_MSD) { // 2
		return MsgMsdNoSupport; // -1017
	}

	ret = PayWave_ReadAppData_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayWave_ReadAppData_Api() failed(%d) !!!", ret);
		goto WAVE_COMPLETED;
	}
	if(isSmallMainScreen())
	{
		ScrClrLine_Api(LINE1, LINE2);
		ScrDisp_Api(LINE2, 0, "Remove card", CDISP);
	}
	else
		ScrDisp_Api(LINE3, 0, "Please remove card", FDISP | CDISP);

	PiccStop();

	GetPanNumber();

	ret = PayWave_ProcRestrictions_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayWave_ProcRestrictions_Api() failed(%d) !!!", ret);
		goto WAVE_COMPLETED;
	}

	ret = PayWave_OfflineDataAuth_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayWave_OfflineDataAuth_Api() failed(%d) !!!", ret);
		goto WAVE_COMPLETED;
	}

	ret = PayWave_VerifyCardholder_Api(&cvm, &needOnline);
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! PayWave_VerifyCardholder_Api() failed(%d) !!!", ret);
		goto WAVE_COMPLETED;
	}

	LogPrintWithRet(0, "cvm = ",        cvm);
	LogPrintWithRet(0, "needOnline = ", needOnline);

	if (cvm == PAYWAVE_CVM_SIGNATURE) { // 1
		// todo for Signature
	}
	else if (cvm == PAYWAVE_CVM_ONLINE_PIN) // 2
	{
		ret = EnterPIN(0);
		if (ret != 0) goto WAVE_COMPLETED;
	}

	onlineResult = ONLINE_APPROVE;
	IccOnline    = needOnline;

	if (IccOnline != 0) {
		//send message to server and get message from server 
		if (onlineResult == ONLINE_FAILED) {
			// todo
		}
		else if(onlineResult == ONLINE_APPROVE) {
			ret = PaywaveTransComplete();
			LogPrintWithRet(0, "PaywaveTransComplete(): ", ret);
		}
	}

	return  ret;

WAVE_COMPLETED:	
	if (ret == ERR_SELECTNEXT) // -114
	{
		ret = PayWave_SelectApp_Api(NULL);
		LogPrintWithRet(0, "PayWave_SelectApp_Api(NULL): ", ret);

		goto WAVE_SELECT_NEXT;
	}

	return ret;
}

int PaywaveTransComplete()
{
	int ret, authCodeLen, scriptLen, needIssuer, onlineResult;

	COMMON_TERMINAL_PARAM param;

	// authCodeLen scriptLen should be real data length got by server response message 
	ret = PayWave_Completion_Api(onlineResult, authCodeLen, scriptLen, &needIssuer);
	if (ret != 0) LogPrintWithRet(1, "!!! PayWave_Completion_Api() failed(%d) !!!",  ret);

	Common_GetParam_Api(&param);

	#ifdef _RECVDATA_DEBUG_
		ret = 0;
	#endif

	// For qVSDC, refund should not be declined for AAC
	if ((ret == ERR_EMVDENIAL) && (param.TransType == 33)) { // -9
		ret = EMV_OK;
	}
	// Offline Declined trans must be uploaded also
	if ((IccOnline == 0) && (ret == ERR_EMVDENIAL)) {
		// todo for: Send message to server and get response
	}

	if (1 == needIssuer) {
		u8 CardType[8];
		u8 SerialNo[64];

		unsigned int timerid;

		ret = PiccInit();
		if (ret != 0){
			TipAndWaitEx_Api("!!! PICC open error !!!");
			return ret;
		}
			
		timerid = TimerSet_Api();
		ScrDisp_Api(LINE4, 0, "Please wave card again", FDISP|LDISP);			

		while (!TimerCheck_Api(timerid, 60 * 1000))
		{
			if (PiccCheck() == 0) {
				int authDataLen = 0, scriptLen1 = 0;  // should got in response message
				u8 authData[128], script[1024];       // should got in response message

				ScrDisp_Api(LINE4, 0, "Processing...", FDISP|LDISP);

				ret = PayWave_ProcIssuerUpdate_Api(authDataLen,authData,scriptLen1,script);
				if (ret == ERR_AGAIN) // -105
				{
					ScrDisp_Api(LINE4, 0, "Please wave card again2", FDISP|LDISP);
					continue;
				}

				TipAndWaitEx_Api("Please remove card");
				break;
			}
		}
	} 

	PiccStop();

	return ret;
}

void initCtlsConfig(int transType)
{
	COMMON_TERMINAL_PARAM termParam;

	PAYPASS_TERM_PARAM ppParam; 
	PAYWAVE_TERM_PARAM pwparm;
	QUICS_TERM_PARAM qcsparm;

	if (transType == 0xFF) { transType = 0x00; }

	// ====================== COMMON PARAM
	Common_GetParam_Api(&termParam);

	termParam.AcquierId[0] = 0;
	memcpy(termParam.MerchName, "Merchant1", strlen("Merchant1"));
	AscToBcd_Api(termParam.MerchCateCode, "0001", 4);
	memcpy(termParam.MerchId, "000001",   strlen("000001"));
	memcpy(termParam.TermId,  "12345678", 8);
	AscToBcd_Api(termParam.CountryCode,   "0840", 4);
	AscToBcd_Api(termParam.TransCurrCode, "0840", 4);
	AscToBcd_Api(termParam.ReferCurrCode, "0840", 4);
	memcpy(termParam.AcquierId, "12345678", 8);
	termParam.ReferCurrExp = 0x02;
	termParam.ReferCurrCon = 1000;
	termParam.TerminalType = 0x22;
	termParam.TransCurrExp = 0x02;
	termParam.TransType    = transType;

	Common_SetParam_Api(&termParam); 
	Common_SaveParam_Api(&termParam);
	// ======================

	// ====================== PAYPASS PARAM
	PayPass_GetParam_Api(&ppParam);

	ppParam.CardDataInputCapability = 0x00;
	ppParam.SecurityCapability      = 0x08;
	memset(ppParam.ExCapability, 0x00, 5);
	ppParam.ReadBalanceBeforeGenAC = 0;
	ppParam.ReadBalanceAfterGenAC  = 0;
	memset(ppParam.BalanceBG, 0xFF, 6);
	memset(ppParam.BalanceAG, 0xFF, 6);
	memset(ppParam.MaxTornTransTime, 0, 2);
	ppParam.MaxTornTransNum = 0;
	memcpy(ppParam.MessageHoldTime, "\x00\x00\x13", 3);
	memcpy(ppParam.MaxRRTGrace,     "\x00\x32",     3);
	memcpy(ppParam.MinRRTGrace,     "\x00\x14", 	3);
	ppParam.RRTThresholdM = 0x32;
	memcpy(ppParam.RRTThresholdA, "\x01\x2c", 2);
	memcpy(ppParam.ExpRRTCAPDU,   "\x00\x12", 2);
	memcpy(ppParam.ExpRRTRAPDU,   "\x00\x18", 2);
	memcpy(ppParam.MerchantCustomData, "\x04\x11\x22\x33\x44", 5);
	ppParam.TransCategoryCode = 0x01;

	PayPass_SetParam_Api(&ppParam);
	PayPass_SaveParam_Api(&ppParam);
	// ======================
	
	// ====================== PAYWAVE PARAM
	PayWave_GetParam_Api(&pwparm);

	AscToBcd_Api(pwparm.TTQ , "3600C000", 8);
	pwparm.bCheckBlacklist = 1;
	pwparm.bDRL 		   = 1;
	pwparm.bCashDRL 	   = 1;
	pwparm.bCashbackDRL    = 1;
	AscToBcd_Api(pwparm.CA_TTQ , "3600C000", 8);
	pwparm.CA_bStatusCheck     = 1;
	pwparm.CA_bZeroAmtCheck    = 1;
	pwparm.CA_ZeroAmtCheckOpt  = 0;
	pwparm.CA_bTransLimitCheck = 1;
	pwparm.CA_bCVMLimitCheck   = 1;
	pwparm.CA_bFloorLimitCheck = 1;
	pwparm.CA_bHasFloorLimit   = 1;
	pwparm.CA_TransLimit 	   = 100000;
	pwparm.CA_CVMLimit 		   = 1000;
	pwparm.CA_FloorLimit 	   = 2000;
	AscToBcd_Api(pwparm.CB_TTQ , "3600C000", 8);
	pwparm.CB_bStatusCheck     = 1;
	pwparm.CB_bZeroAmtCheck    = 1;
	pwparm.CB_ZeroAmtCheckOpt  = 0;
	pwparm.CB_bTransLimitCheck = 1;     
	pwparm.CB_bCVMLimitCheck   = 1;
	pwparm.CB_bFloorLimitCheck = 1;
	pwparm.CB_bHasFloorLimit   = 1;
	pwparm.CB_TransLimit 	   = 5000;
	pwparm.CB_CVMLimit 		   = 1000;
	pwparm.CB_FloorLimit 	   = 2000;

	PayWave_SetParam_Api(&pwparm);
	PayWave_SaveParam_Api(&pwparm);
	// ======================

	// ====================== Quics PARAM
	qUICS_GetParam_Api(&qcsparm);

	memcpy(qcsparm.TTQ, "\x36\x00\x40\x80", 4);
	memcpy(qcsparm.Capability, "\xE0\xE9\xC8", 3);
	memcpy(qcsparm.ExCapability, "\xFF\x80\xF0\xA0\x01", 5);

	qUICS_SetParam_Api(&qcsparm);
	qUICS_SaveParam_Api(&qcsparm);
	// ======================
}

void CTLPreProcess()
{
	int nRet;

	PWaveCLAllowed = 1;
	QuicsCLAllowed = 1;

	nRet = PayPass_PreProcess_Api(PosCom.stTrans.TradeAmount, NULL);
	if (nRet != 0) LogPrintWithRet(1, "!!! PayPass_PreProcess_Api() failed(%d) !!!", nRet);

	nRet = PayWave_PreProcess_Api(PosCom.stTrans.TradeAmount, NULL);
	if (nRet != 0) LogPrintWithRet(1, "!!! PayWave_PreProcess_Api() failed(%d) !!!", nRet);

	if (nRet != EMV_OK) {
		PWaveCLAllowed = 0;
	}

	nRet = qUICS_PreProcess_Api(PosCom.stTrans.TradeAmount, NULL);
	if (nRet != 0) LogPrintWithRet(1, "!!! qUICS_PreProcess_Api() failed(%d) !!!", nRet);

	if (nRet != EMV_OK) {
		QuicsCLAllowed = 0;
	}

	nRet = RUPAY_PreProcess_Api(PosCom.stTrans.TradeAmount, NULL);
	if (nRet != 0) LogPrintWithRet(1, "!!! RUPAY_PreProcess_Api() failed(%d) !!!", nRet);

}

int PayPassCB_DEKDET(int DTSLen, unsigned char *DTS,int DNLen, unsigned char *DN)
{
	return 0;
}

// ===================================== EMV BLOCK =====================================
int EmvSaveData(void)
{
	int ret, iLength = 0 ;
	unsigned char sTemp[32] = {0}, buf[20] = {0};

	if (!PosCom.stTrans.IccOnline && (PosCom.stTrans.Trans_id == POS_SALE))
	{
		ret = Common_GetTLV_Api(0x9F74, sTemp, &iLength);
		if (ret != 0) {
			LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F74) failed(%d) !!!", ret);

			ret = Common_SetTLV_Api(0x9F74, (unsigned char *)"ECC001", 6);
			if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F74, 'ECC001') failed(%d) !!!", ret);
		}

		GetSysTime_Api(sTemp);

		memcpy(PosCom.stTrans.TradeDate, sTemp, 	4);
		memcpy(PosCom.stTrans.TradeTime, &sTemp[4], 3);
	}

	ret = Common_GetTLV_Api(0x5f24, buf, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5f24) failed(%d) !!!", ret);

	memcpy(PosCom.stTrans.ExpDate, buf, 3);

	ret = Common_GetTLV_Api(0x5f20, (u8*)PosCom.stTrans.HoldCardName, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5f20) failed(%d) !!!", ret);

	PosCom.stTrans.HoldCardName[iLength] = 0;

	//ret = Common_GetTLV_Api(0x9f74, (u8*)PosCom.stTrans.AuthCode,	&iLength);
	//if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f74) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9F26, PosCom.stTrans.sAppCrypto, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f26) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x8A, PosCom.stTrans.sAuthRspCode, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x8a) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x95, PosCom.stTrans.sTVR, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x95) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9B, PosCom.stTrans.sTSI, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9b) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9F36, PosCom.stTrans.sATC, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f36) failed(%d) !!!", ret);

	return EMV_OK;
}

void EmvSaveDataOnline(void)
{
    int ret, iLength = 0;
	u8 Buf[100] = {0};

	ret = Common_GetTLV_Api(0x5f24, Buf, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5f24) failed(%d) !!!", ret);

	memcpy(PosCom.stTrans.ExpDate, Buf, 3);

	ret = Common_GetTLV_Api(0x5f20, (u8*)PosCom.stTrans.HoldCardName, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5f20) failed(%d) !!!", ret);

	PosCom.stTrans.HoldCardName[iLength] = 0;

	//ret = Common_GetTLV_Api(0x9f74, (u8*)PosCom.stTrans.AuthCode,	&iLength);
	//if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f74) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9F26, PosCom.stTrans.sAppCrypto, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f26) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x8A, PosCom.stTrans.sAuthRspCode, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x8a) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x95, PosCom.stTrans.sTVR, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x95) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9B, PosCom.stTrans.sTSI, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9b) failed(%d) !!!", ret);

	ret = Common_GetTLV_Api(0x9F36, PosCom.stTrans.sATC, &iLength);
	if (ret != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9f36) failed(%d) !!!", ret);
}

int App_EmvTrans(unsigned char tradetype, unsigned char cardtype, unsigned char *pTrackBuf)
{
	int ret = EMV_OK, nLen;
	u8 sTemp[128];

	PosCom.stTrans.IccOnline = 1;

	LogPrintWithRet(0, "gCtrlParam.lTraceNo = ", gCtrlParam.lTraceNo);

	ret = EMV_SelectApp_Api(0, gCtrlParam.lTraceNo);
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_SelectApp_Api(0) failed(%d) !!!", ret);

RESELECT:
	if (ret != EMV_OK) { goto EMVRESULT; }

	ret = EMV_InitApp_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_InitApp_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	ret = EMV_ReadAppData_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_ReadAppData_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	if (ret != EMV_OK) { goto EMVRESULT; }

	memset(sTemp, 0, sizeof(sTemp));
	ret = PEDReadPinPadSn_Api(sTemp);

	if (ret == 0x00)
	{
		nLen = (sTemp[0] - 0x30) * 10 + (sTemp[1] - 0x30) + 2;

		ret = Common_SetTLV_Api(0x9f1e, sTemp + (nLen - 8), 8);
		if (ret != 0) LogPrintWithRet(1, "!!! Common_SetTLV_Api(0x9f1e) failed(%d) !!!", ret);
	}
	else {
		LogPrintWithRet(1, "!!! PEDReadPinPadSn_Api() failed(%d) !!!", ret);
	}

	LogPrintInfo("========== GET EMV TRACK DATA START ==========");
	ret = GetEmvTrackData(pTrackBuf);
	LogPrintInfo("=========== GET EMV TRACK DATA END ===========");

	if (ret < 0) {
		LogPrintWithRet(1, "!!! GetEmvTrackData() failed(%d) !!!", ret);
		return E_TRANS_FAIL; // 2
	}

	if (cardtype & CARD_EMVSIMPLE) { goto EMVRESULT; }

	ret = EMV_OfflineDataAuth_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_OfflineDataAuth_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	ret = EMV_ProcRestrictions_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_ProcRestrictions_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	ret = EMV_VerifyCardholder_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_VerifyCardholder_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	ret = EMV_RiskManagement_Api();
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_RiskManagement_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	ret = EMV_TermActAnalyse_Api(&PosCom.stTrans.IccOnline);
	if (ret != EMV_OK) {
		LogPrintWithRet(1, "!!! EMV_TermActAnalyse_Api() failed(%d) !!!", ret);
		goto EMVRESULT;
	}

	if (PosCom.stTrans.IccOnline == TRUE) { EmvSaveDataOnline(); }

	EmvSaveData();

EMVRESULT:
	if (ret == ERR_SELECTNEXT) // -114
	{
		ret = EMV_SelectApp_Api(1, gCtrlParam.lTraceNo);
		if (ret != 0) LogPrintWithRet(1, "!!! EMV_SelectApp_Api(1) failed(%d) !!!", ret);

		goto RESELECT;
	}

	return ret;
}

int EmvInitDefParam(void)
{
	COMMON_TERMINAL_PARAM termParam;
	EMV_TERM_PARAM        stEmvParam;

	Common_GetParam_Api(&termParam);
	EMV_GetParam_Api(&stEmvParam);

	sprintf((char *)termParam.MerchName, "%.40s", gCtrlParam.MerchantName);

	memcpy(termParam.MerchCateCode, "\x00\x01", 		   2);
	memcpy(termParam.MerchId,       gCtrlParam.MerchantNo, 15);
	memcpy(termParam.TermId,  		gCtrlParam.TerminalNo, 8);

	strcpy((char *)termParam.AcquierId, "12345678");

	memcpy(termParam.CountryCode,   "\x03\x56", 2);
	memcpy(termParam.TransCurrCode, "\x03\x56", 2);
	memcpy(termParam.ReferCurrCode, "\x03\x56", 2);

	termParam.TransCurrExp = 0x02;
	termParam.ReferCurrExp = 0x02;
	termParam.ReferCurrCon = 1000;
	termParam.TerminalType = 0x22;

	memcpy(stEmvParam.Capability,   "\xE0\xF1\xC8", 		3);
	//memcpy(stEmvParam.ExCapability, "\x60\x00\xF0\xA0\x01", 5);

	stEmvParam.ForceOnline   = 1;
	stEmvParam.GetDataPIN    = 1;
	stEmvParam.SupportPSESel = 1;

	Common_SetParam_Api(&termParam);
	Common_SaveParam_Api(&termParam);

	EMV_SetParam_Api(&stEmvParam);
	EMV_SaveParam_Api(&stEmvParam);

	return 0;
}

int EmvCardProc(u8 tradetype, u8 cardtype, u8 *pTrackBuf)
{
	int iRet = 0, nLen = 0;
	u8 sTemp[256] = {0}, buf[10] = {0};
	unsigned short ulen;

	ScrClrLine_Api(LINE2, LINE5);
	if(isSmallMainScreen())
		ScrDisp_Api(LINE2, 0, "Processing...", CDISP);
	else
	{
		ScrDisp_Api(LINE3, 0, "Processing...", CDISP);
		ScrDisp_Api(LINE4, 0, "Please Wait", CDISP);
	}

	iRet = Common_SetIcCardType_Api(PEDICCARD, 0);
	if (iRet != 0) {
		LogPrintWithRet(1, "!!! Common_SetIcCardType_Api() failed(%d) !!!", iRet);
		return iRet;
	}

	EMV_Clear_Api();
	EMV_SetTradeAmt_Api(PosCom.stTrans.TradeAmount, NULL);

	if (cardtype & CARD_EMVSIMPLE) {
		g_EmvFullOrSim = 0;
	} else {
		g_EmvFullOrSim = 1;
	}

	LogPrintWithRet(0, "g_EmvFullOrSim = ", g_EmvFullOrSim);
	EmvInitDefParam();

	iRet = App_EmvTrans(tradetype, cardtype, pTrackBuf);

	return iRet;
}

int CEmvGetDateTime(unsigned char *DateTime)
{
	unsigned char DataTimeTemp[10];
	memset(DataTimeTemp, 0, sizeof(DataTimeTemp));

	GetSysTime_Api(DataTimeTemp);
	memcpy(DateTime, DataTimeTemp+1, 6);
	return 0;
}

int CEmvReadSN(unsigned char *Sn)
{
	unsigned char SnBuf[50];
	unsigned char Ret;
	int Len;

	if(Sn == NULL)
		return 1;

	memset(SnBuf, 0, sizeof(SnBuf));
	Ret = PEDReadPinPadSn_Api(SnBuf);
	if(Ret == 0)
	{
		Len = AscToLong_Api(SnBuf, 2);
		memcpy(Sn, SnBuf+2+Len-8, 8);
	}
	return Ret;
}

int CEmvGetUnknowTLV(unsigned short Tag, unsigned char *dat, int len)
{
	switch( Tag )
	{
	case 0x9F53:
		*dat=0x52;
		return 0;
	}

	return -1;
}

void CEmvDebugIccCommand(APDU_SEND *send, APDU_RESP *recv, int retValue)
{
	// todo
}

int GetEmvTrackData(u8 * pTrackBuf)
{
	int iRet = 0, iLength = 0;
    u8 sTemp[30], cHaveTrack2 = 0, cReadPan = 0, cRet = 0;

    cHaveTrack2 = 0;
    cReadPan = 0;

    // Read Track 2 Equivalent Data
    memset(sTemp, 0, sizeof(sTemp));

    iRet = Common_GetTLV_Api(0x57, sTemp, &iLength);
    if (iRet == EMV_OK)
    {
    	cHaveTrack2 = 1;

	    pTrackBuf[0] = iLength;
	    memcpy(&pTrackBuf[1], sTemp, iLength);
	    pTrackBuf[iLength+1] = 0;	//
    }
    else {
        LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x57) failed(%d) !!!", iRet);
    }

    // read PAN
    memset(sTemp, 0, sizeof(sTemp));
    iRet = Common_GetTLV_Api(0x5A, &sTemp[1], &iLength);

	sTemp[0] = iLength;

	LogPrintWithRet(0, "cHaveTrack2 = ", cHaveTrack2);

    if (iRet == EMV_OK)
    {
    	if (cHaveTrack2)
	 	{
    		cRet = MatchTrack2AndPan(pTrackBuf, sTemp);
			if (cRet != 0)
			{
				LogPrintWithRet(1, "!!! MatchTrack2AndPan() failed(%d) !!!", cRet);
				return -1;
			}
	 	}

		cReadPan = 1;
    }
	else {
	    LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5A) failed(%d) !!!", iRet);
		if (cHaveTrack2 == 0) return -2;
	}

	// read PAN sequence number
	iRet = Common_GetTLV_Api(0x5F34, &PosCom.stTrans.ucPanSeqNo, &iLength);

	if (iRet == EMV_OK)
		PosCom.stTrans.bPanSeqNoOk = 1;
	else
		PosCom.stTrans.bPanSeqNoOk = 0;

	LogPrintWithRet(0, "cReadPan = ", cReadPan);

	// read Application Expiration Date
	if (cReadPan)
	{
		memset(sTemp, 0, sizeof(sTemp));
		iRet = Common_GetTLV_Api(0x5F24, sTemp, &iLength);

		if (iRet == EMV_OK) {
			memcpy(PosCom.stTrans.ExpDate, sTemp, 3);
		} else {
			LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5F24) failed(%d) !!!", iRet);
		}
	}

	// read other data for print slip
	iRet = Common_GetTLV_Api(0x50, (u8*)PosCom.stTrans.szAppLable, &iLength);
	if (iRet != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x50) failed(%d) !!!", iRet);

	// read application preferred name
	Common_GetTLV_Api(0x9F12, PosCom.stTrans.szAppPreferName, &iLength);
	if (iRet != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F12) failed(%d) !!!", iRet);

	iRet = Common_GetTLV_Api(0x4F, sTemp, &iLength);
	if (iRet == EMV_OK)
	{
		BcdToAsc_Api(PosCom.stTrans.szAID, sTemp, (iLength * 2));
		RemoveTailChars(PosCom.stTrans.szAID, 'F');
	}
	else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x4F) failed(%d) !!!", iRet);
	}

	iRet = Common_GetTLV_Api(0x82, PosCom.stTrans.sAIP, &iLength);
	if (iRet != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x82) failed(%d) !!!", iRet);

	iRet = Common_GetTLV_Api(0x9F37, PosCom.stTrans.szUnknowNum, &iLength);
	if (iRet != 0) LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F37) failed(%d) !!!", iRet);

	return 0;
}

unsigned char GetPosEntryMode(void)
{
	// fallback: 0x92, magstrip_notfallback: 0x02, chip: 0x05
	if (PosCom.stTrans.EntryMode[0] == PAN_ICCARD ||
		PosCom.stTrans.EntryMode[0] == PAN_PAYPASS ||
		PosCom.stTrans.EntryMode[0] == PAN_PAYWAVE)
	{
		return 0x05;
	}
	else if (PosCom.stTrans.EntryMode[0] == PAN_MAGCARD)
	{
		if (PosCom.stTrans.IccFallBack == 1) { return 0x92; }
		else { return 0x02; }
	}
	else {
		return 0;
	}
}

unsigned char GetPosBatchCaptureInfo(void)
{
     return 0; // TermInfo.bBatchCapture;
}

unsigned char GetPosAdviceSupportInfo(void)
{
     return 1; // TermInfo.bSurportAdvices;
}

int GetCardFromTrack(u8 *CardNo, u8 *track2, u8 *track3)
{
	int i;

	track2[37]  = 0;
	track3[104] = 0;

	if (strlen((char*)track2) != 0)
	{
		i = 0;
		while (track2[i] != '=')
		{
			if (i > 19) { return E_ERR_SWIPE; }
			i++;
		}
		if (i < 13 || i > 19) { return E_ERR_SWIPE; }

		memcpy(CardNo, track2, i);
		CardNo[i] = 0;
	}
	else if (strlen((char*)track3) != 0)
	{
		i = 0;
		while (track3[i] != '=')
		{
			if (i > 21) { return E_ERR_SWIPE; }
			i++;
		}
		if (i < 15 || i > 21) { return E_ERR_SWIPE; }

		memcpy(CardNo, track3 + 2, i - 2);
		CardNo[i - 2] = 0;
	}

	return 0;
}

void FormatAmtToDisp_Api(char *pOut, u8 *pIn, u8 flag)
{
	u8 len;
	char stemp1[64], stemp2[64];

	memset(stemp1, 0, sizeof(stemp1));
	memset(stemp2, 0, sizeof(stemp2));

	if (flag == 0) {
		BcdToAsc_Api(stemp1, pIn, 12);
	}
	else {
		len = strlen((char*)pIn);
		if (len > 12) { len = 12; }

		memcpy(stemp1, pIn, len);
	}

	FormatAmt_Str(stemp2, stemp1);
	strcpy(pOut, stemp2);
}

void FormatAmt_Str(char *pDest, char *pSrc)
{
	unsigned char i = 0;
	unsigned char cLen = 0;

	cLen = strlen(pSrc);
	for (i = 0; i < cLen; i++) {
		if ((pSrc[i] != '0') || (pSrc[i] == 0)) { break; }
	}
	pSrc += i;
	cLen = strlen(pSrc);

	strcpy(pDest, "0.00");

	if (cLen > 2) {
		memcpy(pDest, pSrc, cLen - 2);
		pDest[cLen-2] = '.';
		strcpy(&pDest[cLen-1], &pSrc[cLen-2]);
	}
	else {
		memcpy(&pDest[4-cLen], pSrc, cLen);
	}
}
// ===================================== EMV BLOCK =====================================
