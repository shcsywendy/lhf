/*
 * basePipe hpp + cpp protoype and define a base class for building
 * pipeline functions to execute
 *
 */
#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <random>
#include <chrono>
#include <string>
#include <numeric>
#include <iostream>
#include <functional>
#include <vector>
#include <queue>

#include "kdTree.hpp"
#include "utils.hpp"

kdTree::kdTree(){
 //   procName = "kdTree";
    return;
} 
// create a binary tree of nodes of "k" dimensions from points of the input csv
//every leaf node is a k dimensional point, non leaf nodes splits hyperplane of space
// each node represents axis aligned hyper rectangle 
// each node specifies an axis and splits the set of points based on whether
//their coordinate along that axis is > || < a certain value
//axis and splitting point chosen by sliding midpoint rule -> split along longest side of node
// ie divide perpendicular to longest spread 
//docs.scipy.org/doc/scipy-0.14.0/reference/generated/scipy.spatial.KDTree.html

//kdtree is used to find nearest neighbors to current point based on epsilon

//avdongre.wordpress.com/2011/06/14/kd-tree-in-c/
// github.com/crvs/KDTree/blob/master/KDTree.cpp#L41
utils ut;
using point = std::vector<double>;
using pointList = std::vector<std::vector<double>>;
using pointListItr =  pointList::iterator; //alias declarations

kdNode::kdNode() = default;

kdNode::kdNode(const point &pt, const size_t &idx_, const kdNodePtr &left_, const kdNodePtr &right_){
    x = pt;
    index = idx_;
    left = left_;
    right = right_;
}

kdNode::kdNode(const pointIndex &pi, const kdNodePtr &left_, const kdNodePtr &right_){
    x = pi.first;
    index = pi.second;
    left = left_;
    right = right_;
}

kdNode::~kdNode() = default; //destructor
//operator overloads so types don't get messed up
kdNode::operator bool() { return (!x.empty()); }
kdNode::operator point() { return x; }
kdNode::operator size_t() { return index; }
kdNode::operator pointIndex() { return pointIndex(x, index); }


kdNodePtr newKdNodePtr(){
    return std::make_shared<kdNode>();
}

bool sortByLevel(const pointIndex &lhs, pointIndex &rhs, const size_t &level){
    return lhs.first[level] < rhs.first[level];
}


kdNodePtr kdTree::makeTree(const pointIndexArr::iterator &begin, const pointIndexArr::iterator &end, const size_t &length, const size_t &level){
    if(begin == end){
        return newKdNodePtr(); // sanity check for empty list
    }

    size_t dim = begin->first.size();

    if(length > 1){
        using namespace std::placeholders;
        std::sort(begin, end, std::bind(sortByLevel, _1, _2, level));
    }

    auto middle = begin + (length/2);

    auto leftBegin = begin;
    auto leftEnd = middle;
    auto rightBegin = middle + 1;
    auto rightEnd = end;

    size_t leftLength = length/2;
    size_t rightLength = length - leftLength -1;

    kdNodePtr left; //recursively build left and right branches until leaf reached
    if (leftLength > 0 && dim > 0){
        left = makeTree(leftBegin, leftEnd, leftLength, (level+1) % dim);
    } else{
       left = newKdNodePtr();
    }

    kdNodePtr right; 
    if(rightLength > 0 && dim > 0){
        right = makeTree(rightBegin, rightEnd, rightLength, (level+1) % dim);
    } else{
       right = newKdNodePtr();
    }

    return std::make_shared< kdNode >(*middle, left, right);
    //returns pointer to root of the constructed kd tree
}

//driver code, push back inData.originalData to point vec so we can put in tree
kdTree::kdTree(pipePacket inData){
    pointIndexArr arr;

    for(size_t i=0; i<inData.originalData.size(); i++){
        arr.push_back(pointIndex(inData.originalData[i],  i));
    }

    auto begin = arr.begin();
    auto end = arr.end();
    
    size_t length = arr.size();
    size_t level = 0; 

    root = kdTree::makeTree(begin, end, length, level);

}

kdTree::kdTree(pointVec inData, int size = 0){
    if(size == 0){
        size = inData.size();
    }

    pointIndexArr arr;

    for(size_t i=0; i<size; i++){
        arr.push_back(pointIndex(inData[i],  i));
    }

    auto begin = arr.begin();
    auto end = arr.end();
    
    size_t length = arr.size();
    size_t level = 0; 

    root = kdTree::makeTree(begin, end, length, level);

}

//find nearest neighbor to points - need to pick correct branch

kdNodePtr kdTree::findNearest(const kdNodePtr &branch, const point &pt, const size_t &level, const kdNodePtr &best, const double &bestDist){
    double d, dx, dsquared;

    if (!bool(*branch)){
        return newKdNodePtr(); // basically, null
    }

    point branch_pt(*branch);
    size_t dim = branch_pt.size();

    d = ut.vectors_distance(branch_pt, pt);
    dx = branch_pt.at(level) - pt.at(level);

    kdNodePtr bestL = best;
    double bestDistL = bestDist;

    if(d < bestDist){
        bestDistL = d;
        bestL = branch;
    }

    size_t nextLv = (level+1)%dim;
    kdNodePtr section;
    kdNodePtr other;

    if (dx>0){ //check branch for correct one to search
        section = branch-> left;
        other = branch -> right;
    }
    else{
        section = branch->right;
        other = branch->left;
    }

    //traverse further down the tree
    kdNodePtr further = findNearest(section, pt, nextLv, bestL, bestDistL);
    if(!further -> x.empty()){
        double dl = ut.vectors_distance(further->x, pt);
        if(dl < bestDistL){
            bestDistL = dl;
            bestL = further;
        }
    }

    if(abs(dx) < bestDistL){
        further = findNearest(other, pt, nextLv, bestL, bestDistL);
        if(!further->x.empty()){
            double dl = ut.vectors_distance(further->x, pt);
            if(dl < bestDistL){
                bestDistL = dl;
                bestL = further;
            }
        }
    }

    return bestL;
}

//default caller https://github.com/crvs/KDTree/blob/master/KDTree.cpp#L50

kdNodePtr kdTree::nearest(const point &pt) {
    size_t level = 0;
    double branchDist = ut.vectors_distance(point(*root), pt);
    return findNearest(root, pt, level, root, branchDist); // best is root of current query tree
}

point kdTree::nearestPoint(const point &pt){
    return point(*nearest(pt));
}

size_t kdTree::nearestIndex(const point &pt){
    return size_t(*nearest(pt));
}

pointIndex kdTree::nearestPointIndex(const point &pt){
    return pointIndex(*nearest(pt));
}

pointIndexArr kdTree::neighborhood(const kdNodePtr &branch, const point &pt, const double &rad, const size_t &level){
    double d, dx, dsquared;

    if(!bool(*branch)) {
        return pointIndexArr();
    }  //check for empty branch

    size_t dim = pt.size();
    d = ut.vectors_distance(point(*branch), pt);
    dx = point(*branch).at(level) - pt.at(level);

    pointIndexArr nbh, nbh_s, nbh_o;
    if (d <= rad) {
        nbh.push_back(pointIndex(*branch));
    }

    kdNodePtr section; 
    kdNodePtr other;

    if(dx > 0) {
        section = branch-> left;
        other = branch -> right;
    } 
    else {
        section = branch -> right;
        other = branch -> left;
    }

    nbh_s = neighborhood(section, pt, rad, (level +1) % dim);
    nbh.insert(nbh.end(), nbh_s.begin(), nbh_s.end());
    if(abs(dx) < rad){
        nbh_o = neighborhood(other, pt, rad, (level+1)% dim);
        nbh.insert(nbh.end(), nbh_o.begin(), nbh_o.end());
    }


    return nbh;
}

pointIndexArr kdTree::neighborhood(const point &pt, const double &rad){
    return neighborhood(root, pt, rad, 0);
}

pointVec kdTree::neighborhoodPoints(const point &pt, const double &rad){
    pointIndexArr nbh = neighborhood(pt, rad);
    pointVec nbhp(nbh.size());
    std::transform(nbh.begin(), nbh.end(), nbhp.begin(), [](pointIndex x) { return x.first; });
    return nbhp;
}

indexArr kdTree::neighborhoodIndices(const point &pt, const double &rad){
    pointIndexArr nbh = neighborhood(pt, rad);
    indexArr nbhi(nbh.size());
    std::transform(nbh.begin(), nbh.end(), nbhi.begin(), [](pointIndex x) { return x.second; });
    return nbhi;
}

/*pipePacket kdTree::runPreprocessor(pipePacket inData){

} 
 



// configPipe -> configure the function settings of this pipeline segment
bool kdTree::configPreprocessor(std::map<std::string, std::string> configMap)
{
    std::string strDebug;

    auto pipe = configMap.find("debug");
    if (pipe != configMap.end())
    {
        debug = std::atoi(configMap["debug"].c_str());
        strDebug = configMap["debug"];
    }
    pipe = configMap.find("outputFile");
    if (pipe != configMap.end())
        outputFile = configMap["outputFile"].c_str();


  //  ut.writeDebug("StreamKMeans", "Configured with parameters { clusters: " + configMap["clusters"] + ", iterations: " + configMap["iterations"] + ", debug: " + strDebug + ", outputFile: " + outputFile + " }");

}  */
