/*
 * LinkList.cpp
 *
 *  Created on: Jun 13, 2012
 *      Author: cryan
 *  Stripping out HDF5 to use simple binary formate
 *      Graham Rowlands Feb. 2019
 */

#include "LLBank.h"

LLBank::LLBank() : length{0}, addr_(0), count_(0), repeat_(0),trigger1_(0), trigger2_(0) {
	// TODO Auto-generated constructor stub
}

LLBank::LLBank(const WordVec & addr, const WordVec & count, const WordVec & trigger, const WordVec & repeat) :
		length(addr.size()), IQMode(false), addr_(addr), count_(count), repeat_(repeat), trigger1_(trigger){
	init_data();
};

LLBank::LLBank(const WordVec & addr, const WordVec & count, const WordVec & trigger1, const WordVec & trigger2, const WordVec & repeat) :
		length(addr.size()), IQMode(true), addr_(addr), count_(count), repeat_(repeat), trigger1_(trigger1), trigger2_(trigger2){
	init_data();
};

LLBank::~LLBank() {
	// TODO Auto-generated destructor stub
}

void LLBank::clear(){
	length = 0;
	addr_.clear();
	count_.clear();
	repeat_.clear();
	trigger1_.clear();
	trigger2_.clear();
	packedData_.clear();
	miniLLStartIdx.clear();
	miniLLLengths.clear();

}

WordVec LLBank::get_packed_data(const size_t & startIdx, const size_t & stopIdx){
	//Pull out packed data starting at startIdx but not inclusive of stopIdx
	WordVec vecOut;
	//If we are in IQ mode then we have an extra word for every entry
	int lengthMult = IQMode ? 5 : 4;
	//Handle wrapping around the top of the LL data
	if (stopIdx < startIdx){
		vecOut.assign(packedData_.begin()+lengthMult*startIdx, packedData_.end());
		vecOut.insert(vecOut.end(), packedData_.begin(), packedData_.begin()+lengthMult*stopIdx);
	}
	else{
		vecOut.assign(packedData_.begin()+lengthMult*startIdx, packedData_.begin()+lengthMult*stopIdx);
	}
	return vecOut;
}

int LLBank::write_state_to_file(std::fstream &file){
	throw runtime_error("write_state_to_file not currently implemented.");
}

int LLBank::read_state_from_file(std::fstream &file){
	uint64_t numKeys;
	char keyName[32];
	file.read(reinterpret_cast<char *> (&numKeys), sizeof(uint64_t));
	file.read(reinterpret_cast<char *> (&length), sizeof(uint64_t));
	FILE_LOG(logDEBUG1) << "LL keys: " << numKeys << " length: " << length;
	std::map<std::string, WordVec *> vecForKeyName;
	std::map<std::string, WordVec *>::iterator it;
	vecForKeyName["addr"] = &addr_;
	vecForKeyName["count"] = &count_;
	vecForKeyName["trigger1"] = &trigger1_;
	vecForKeyName["trigger2"] = &trigger2_;
	vecForKeyName["repeat"] = &repeat_;
	for(uint64_t keyct=0; keyct<numKeys; keyct++){
		file.read(reinterpret_cast<char *> (&keyName), 32*sizeof(char));
		string name_str(keyName);
		name_str = name_str.substr(0, name_str.find("#"));
		FILE_LOG(logDEBUG1) << "Read key: " << name_str;
		it = vecForKeyName.find(name_str);
		if (it == vecForKeyName.end()) throw runtime_error("Found improper key!");
		vecForKeyName[name_str]->resize(length);
		file.read(reinterpret_cast<char *> (vecForKeyName[name_str]->data()), length*sizeof(uint16_t));
	}
	try {
		init_data();
	} catch (std::exception & e) {
		return -4;
	}
	return 0;
}

void LLBank::init_data(){

	//Sort out the length of the mini LL's and their start points
	//Go through the LL entries and calculate lengths and start points of each miniLL
	miniLLLengths.clear();
	miniLLStartIdx.clear();
	const USHORT startMiniLLMask = (1 << 15);
	const USHORT endMiniLLMask = (1 << 14);
	size_t lengthCt = 0;
	for(size_t ct = 0; ct < length; ct++){
		// flags are stored in repeat vector
		USHORT curWord = repeat_[ct];
		if (curWord & startMiniLLMask){
			miniLLStartIdx.push_back(ct);
			lengthCt = 0;
		}
		lengthCt++;
		if (curWord & endMiniLLMask){
			miniLLLengths.push_back(lengthCt);
		}
	}
	numMiniLLs = miniLLLengths.size();
	//Now pack the data for writing to the device
	packedData_.clear();
	size_t expectedLength = IQMode ? 5*length : 4*length;
	packedData_.reserve(expectedLength);

	for(size_t ct=0; ct<length; ct++){
		packedData_.push_back(addr_[ct]);
		packedData_.push_back(count_[ct]);
		packedData_.push_back(trigger1_[ct]);
		if (IQMode){
			packedData_.push_back(trigger2_[ct]);
		}
		packedData_.push_back(repeat_[ct]);
	}
}
