/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "geometry_octree.h"

// todo: break dependency
#include "PCCTMC3Encoder.h"

#include "OctreeNeighMap.h"
#include "io_hls.h"
#include "tables.h"

namespace pcc {

//============================================================================

class GeometryOctreeEncoder {
public:
  GeometryOctreeEncoder(o3dgc::Arithmetic_Codec* arithmeticEncoder);

  int encodePositionLeafNumPoints(int count);

  void encodeOccupancyNeighZ(int mappedOccupancy);

  void encodeOccupancyNeighNZ(int mappedOccupancy, int neighPattern10);

  void encodeGeometryOccupancy(const PCCOctree3Node& node0, int occupancy);

  void encodePointPosition(int nodeSizeLog2, const PCCVector3<uint32_t>& pos);

  bool encodeDirectPosition(
    int nodeSizeLog2,
    const PCCOctree3Node& node,
    const PCCPointSet3& pointCloud);

private:
  o3dgc::Arithmetic_Codec* _arithmeticEncoder;
  o3dgc::Static_Bit_Model _ctxEquiProb;
  o3dgc::Adaptive_Bit_Model _ctxSingleChild;
  o3dgc::Adaptive_Bit_Model _ctxSinglePointPerBlock;
  o3dgc::Adaptive_Bit_Model _ctxPointCountPerBlock;
  o3dgc::Adaptive_Bit_Model _ctxBlockSkipTh;
  o3dgc::Adaptive_Bit_Model _ctxNumIdcmPointsEq1;
  CtxModelOctreeOccupancy _ctxOccupancy;
};

//============================================================================

GeometryOctreeEncoder::GeometryOctreeEncoder(
  o3dgc::Arithmetic_Codec* arithmeticEncoder)
  : _arithmeticEncoder(arithmeticEncoder)
{}

//============================================================================
// Encode the number of points in a leaf node of the octree.

int
GeometryOctreeEncoder::encodePositionLeafNumPoints(int count)
{
  if (count == 1) {
    _arithmeticEncoder->encode(1, _ctxSinglePointPerBlock);
  } else {
    _arithmeticEncoder->encode(0, _ctxSinglePointPerBlock);
    _arithmeticEncoder->ExpGolombEncode(
      uint32_t(count - 1), 0, _ctxEquiProb, _ctxPointCountPerBlock);
  }

  return count;
}

//-------------------------------------------------------------------------
// encode occupancy bits (neighPattern10 == 0 case)

void
GeometryOctreeEncoder::encodeOccupancyNeighZ(int mappedOccupancy)
{
  int occupancyB0 = !!(mappedOccupancy & 2);
  int occupancyB1 = !!(mappedOccupancy & 128);
  int occupancyB2 = !!(mappedOccupancy & 32);
  int occupancyB3 = !!(mappedOccupancy & 8);
  int occupancyB4 = !!(mappedOccupancy & 4);
  int occupancyB5 = !!(mappedOccupancy & 16);
  int occupancyB6 = !!(mappedOccupancy & 64);
  int occupancyB7 = !!(mappedOccupancy & 1);

  int numOccupiedAcc = 0;

  _arithmeticEncoder->encode(occupancyB0, _ctxOccupancy.b0[numOccupiedAcc]);
  numOccupiedAcc += occupancyB0;

  _arithmeticEncoder->encode(occupancyB1, _ctxOccupancy.b1[numOccupiedAcc]);
  numOccupiedAcc += occupancyB1;

  _arithmeticEncoder->encode(occupancyB2, _ctxOccupancy.b2[numOccupiedAcc]);
  numOccupiedAcc += occupancyB2;

  _arithmeticEncoder->encode(occupancyB3, _ctxOccupancy.b3[numOccupiedAcc]);
  numOccupiedAcc += occupancyB3;

  _arithmeticEncoder->encode(occupancyB4, _ctxOccupancy.b4[numOccupiedAcc]);
  numOccupiedAcc += occupancyB4;

  _arithmeticEncoder->encode(occupancyB5, _ctxOccupancy.b5[numOccupiedAcc]);
  numOccupiedAcc += occupancyB5;

  // NB: There must be at least two occupied child nodes
  //  -- avoid coding the occupancyB if it is implied.
  if (numOccupiedAcc >= 1)
    _arithmeticEncoder->encode(occupancyB6, _ctxOccupancy.b6[numOccupiedAcc]);
  numOccupiedAcc += occupancyB6;

  if (numOccupiedAcc >= 2)
    _arithmeticEncoder->encode(occupancyB7, _ctxOccupancy.b7[numOccupiedAcc]);
}

//-------------------------------------------------------------------------
// encode occupancy bits (neighPattern10 != 0 case)

void
GeometryOctreeEncoder::encodeOccupancyNeighNZ(
  int mappedOccupancy, int neighPattern10)
{
  int occupancyB0 = !!(mappedOccupancy & 2);
  int occupancyB1 = !!(mappedOccupancy & 128);
  int occupancyB2 = !!(mappedOccupancy & 32);
  int occupancyB3 = !!(mappedOccupancy & 8);
  int occupancyB4 = !!(mappedOccupancy & 4);
  int occupancyB5 = !!(mappedOccupancy & 16);
  int occupancyB6 = !!(mappedOccupancy & 64);
  int occupancyB7 = !!(mappedOccupancy & 1);

  uint32_t partialOccupancy = 0;
  int idx;

  idx = neighPattern10;
  _arithmeticEncoder->encode(occupancyB0, _ctxOccupancy.b0[idx]);
  partialOccupancy |= occupancyB0;

  idx = (neighPattern10 << 1) + partialOccupancy;
  _arithmeticEncoder->encode(occupancyB1, _ctxOccupancy.b1[idx]);
  partialOccupancy |= occupancyB1 << 1;

  idx = (neighPattern10 << 2) + partialOccupancy;
  _arithmeticEncoder->encode(occupancyB2, _ctxOccupancy.b2[idx]);
  partialOccupancy |= occupancyB2 << 2;

  idx = (neighPattern10 << 3) + partialOccupancy;
  _arithmeticEncoder->encode(occupancyB3, _ctxOccupancy.b3[idx]);
  partialOccupancy |= occupancyB3 << 3;

  // todo(df): merge constants into lut.
  idx = ((neighPattern10 - 1) << 4) + partialOccupancy;
  idx = kOccMapBit4CtxIdx[idx] - 1 + 5;
  _arithmeticEncoder->encode(occupancyB4, _ctxOccupancy.b4[idx]);
  partialOccupancy |= occupancyB4 << 4;

  idx = ((neighPattern10 - 1) << 5) + partialOccupancy;
  idx = kOccMapBit5CtxIdx[idx] - 1 + 6;
  _arithmeticEncoder->encode(occupancyB5, _ctxOccupancy.b5[idx]);
  partialOccupancy |= occupancyB5 << 5;

  int neighPattern7 = kNeighPattern10to7[neighPattern10];
  idx = ((neighPattern7 - 1) << 6) + partialOccupancy;
  idx = kOccMapBit6CtxIdx[idx] - 1 + 7;
  _arithmeticEncoder->encode(occupancyB6, _ctxOccupancy.b6[idx]);
  partialOccupancy |= occupancyB6 << 6;

  int neighPattern5 = kNeighPattern7to5[neighPattern7];
  idx = ((neighPattern5 - 1) << 7) + partialOccupancy;
  idx = kOccMapBit7CtxIdx[idx] - 1 + 8;
  // NB: if firt 7 bits are 0, then the last is implicitly 1.
  if (partialOccupancy)
    _arithmeticEncoder->encode(occupancyB7, _ctxOccupancy.b7[idx]);
}

//-------------------------------------------------------------------------
// decode node occupancy bits
//

void
GeometryOctreeEncoder::encodeGeometryOccupancy(
  const PCCOctree3Node& node0, int occupancy)
{
  // code occupancy using the neighbour configuration context
  // with reduction from 64 states to 10.
  int neighPattern = node0.neighPattern;
  int neighPattern10 = kNeighPattern64to10[neighPattern];

  uint32_t mappedOccupancy = mapGeometryOccupancy(occupancy, neighPattern);

  if (neighPattern10 == 0) {
    bool singleChild = !popcntGt1(occupancy);
    _arithmeticEncoder->encode(singleChild, _ctxSingleChild);

    if (singleChild) {
      // no siblings => encode index = (z,y,x) not 8bit pattern
      _arithmeticEncoder->encode(!!(occupancy & 0xaa), _ctxEquiProb);  // z
      _arithmeticEncoder->encode(!!(occupancy & 0xcc), _ctxEquiProb);  // y
      _arithmeticEncoder->encode(!!(occupancy & 0xf0), _ctxEquiProb);  // x
    } else {
      encodeOccupancyNeighZ(mappedOccupancy);
    }
  } else {
    encodeOccupancyNeighNZ(mappedOccupancy, neighPattern10);
  }
}

//-------------------------------------------------------------------------
// Encode a position of a point in a given volume.

void
GeometryOctreeEncoder::encodePointPosition(
  int nodeSizeLog2, const PCCVector3<uint32_t>& pos)
{
  for (int mask = 1 << (nodeSizeLog2 - 1); mask; mask >>= 1) {
    _arithmeticEncoder->encode(!!(pos[0] & mask), _ctxEquiProb);
    _arithmeticEncoder->encode(!!(pos[1] & mask), _ctxEquiProb);
    _arithmeticEncoder->encode(!!(pos[2] & mask), _ctxEquiProb);
  }
}

//-------------------------------------------------------------------------
// Direct coding of position of points in node (early tree termination).

bool
GeometryOctreeEncoder::encodeDirectPosition(
  int nodeSizeLog2, const PCCOctree3Node& node, const PCCPointSet3& pointCloud)
{
  int numPoints = node.end - node.start;
  if (numPoints > MAX_NUM_DM_LEAF_POINTS) {
    _arithmeticEncoder->encode(0, _ctxBlockSkipTh);
    return false;
  }

  _arithmeticEncoder->encode(1, _ctxBlockSkipTh);
  _arithmeticEncoder->encode(numPoints > 1, _ctxNumIdcmPointsEq1);

  for (auto idx = node.start; idx < node.end; idx++) {
    // determine the point position relative to box edge
    encodePointPosition(
      nodeSizeLog2,
      PCCVector3<uint32_t>{int(pointCloud[idx][0]) - node.pos[0],
                           int(pointCloud[idx][1]) - node.pos[1],
                           int(pointCloud[idx][2]) - node.pos[2]});
  }

  return true;
}

//-------------------------------------------------------------------------

void
PCCTMC3Encoder3::encodePositions(PayloadBuffer* buf)
{
  uint32_t maxBB =
    std::max({// todo(df): confirm minimum of 1 isn't needed
              1u, boundingBox.max[0], boundingBox.max[1], boundingBox.max[2]});

  // the current node dimension (log2) encompasing maxBB
  int nodeSizeLog2 = ceillog2(maxBB + 1);

  GeometryBrickHeader gbh;
  gbh.geom_geom_parameter_set_id = _gps->gps_geom_parameter_set_id;
  gbh.geomBoxOrigin.x() = int(minPositions.x());
  gbh.geomBoxOrigin.y() = int(minPositions.y());
  gbh.geomBoxOrigin.z() = int(minPositions.z());
  gbh.geom_box_log2_scale = 0;
  gbh.geom_max_node_size_log2 = nodeSizeLog2;
  gbh.geom_num_points = int(pointCloud.getPointCount());
  write(*_gps, gbh, buf);

  // todo(df): remove estimate when arithmetic codec is replaced
  int maxAcBufLen = int(pointCloud.getPointCount()) * 3 * 4 + 1024;
  o3dgc::Arithmetic_Codec arithmeticEncoder(maxAcBufLen, nullptr);
  arithmeticEncoder.start_encoder();
  GeometryOctreeEncoder encoder(&arithmeticEncoder);

  // init main fifo
  //  -- worst case size is the last level containing every input poit
  //     and each point being isolated in the previous level.
  pcc::ringbuf<PCCOctree3Node> fifo(pointCloud.getPointCount() + 1);

  // push the first node
  fifo.emplace_back();
  PCCOctree3Node& node00 = fifo.back();
  node00.start = uint32_t(0);
  node00.end = uint32_t(pointCloud.getPointCount());
  node00.pos = uint32_t(0);
  node00.neighPattern = 0;
  node00.numSiblingsPlus1 = 8;
  node00.siblingOccupancy = 0;

  // map of pointCloud idx to DM idx, used to reorder the points
  // after coding.
  std::vector<int> pointIdxToDmIdx(int(pointCloud.getPointCount()), -1);
  int nextDmIdx = 0;

  size_t processedPointCount = 0;
  std::vector<uint32_t> values;

  auto fifoCurrLvlEnd = fifo.end();

  // this counter represents fifo.end() - fifoCurrLvlEnd().
  // ie, the number of nodes added to the next level of the tree
  int numNodesNextLvl = 0;

  MortonMap3D occupancyAtlas;
  if (_gps->neighbour_avail_boundary_log2) {
    occupancyAtlas.resize(_gps->neighbour_avail_boundary_log2);
    occupancyAtlas.clear();
  }
  PCCVector3<uint32_t> occupancyAtlasOrigin(0xffffffff);

  for (; !fifo.empty(); fifo.pop_front()) {
    if (fifo.begin() == fifoCurrLvlEnd) {
      // transition to the next level
      fifoCurrLvlEnd = fifo.end();
      numNodesNextLvl = 0;
      occupancyAtlasOrigin = 0xffffffff;
      nodeSizeLog2--;
    }

    PCCOctree3Node& node0 = fifo.front();

    // split the current node into 8 children
    //  - perform an 8-way counting sort of the current node's points
    //  - (later) map to child nodes
    int childSizeLog2 = nodeSizeLog2 - 1;
    std::array<int, 8> childCounts = {};
    countingSort(
      PCCPointSet3::iterator(&pointCloud, node0.start),
      PCCPointSet3::iterator(&pointCloud, node0.end), childCounts,
      [=](const PCCPointSet3::Proxy& proxy) {
        const auto& point = *proxy;
        int bitpos = 1 << childSizeLog2;
        return !!(int(point[2]) & bitpos) | (!!(int(point[1]) & bitpos) << 1)
          | (!!(int(point[0]) & bitpos) << 2);
      });

    // generate the bitmap of child occupancy and count
    // the number of occupied children in node0.
    int occupancy = 0;
    int numSiblings = 0;
    for (int i = 0; i < 8; i++) {
      if (childCounts[i]) {
        occupancy |= 1 << i;
        numSiblings++;
      }
    }

    if (_gps->neighbour_avail_boundary_log2) {
      updateGeometryOccupancyAtlas(
        node0.pos, nodeSizeLog2, fifo, fifoCurrLvlEnd, &occupancyAtlas,
        &occupancyAtlasOrigin);

      node0.neighPattern =
        makeGeometryNeighPattern(node0.pos, nodeSizeLog2, occupancyAtlas);
    }

    // encode child occupancy map
    assert(occupancy > 0);
    encoder.encodeGeometryOccupancy(node0, occupancy);

    // when nodeSizeLog2 == 1, children are indivisible (ie leaf nodes)
    // and are immediately coded.  No further splitting occurs.
    if (nodeSizeLog2 == 1) {
      for (int i = 0; i < 8; i++) {
        if (!childCounts[i]) {
          // child is empty: skip
          continue;
        }

        // if the bitstream is configured to represent unique points,
        // no point count is sent.
        if (_gps->geom_unique_points_flag) {
          assert(childCounts[i] == 1);
          continue;
        }

        encoder.encodePositionLeafNumPoints(childCounts[i]);
        processedPointCount += childCounts[i];
      }

      // leaf nodes do not get split
      continue;
    }

    // nodeSizeLog2 > 1: for each child:
    //  - determine elegibility for IDCM
    //  - directly code point positions if IDCM allowed and selected
    //  - otherwise, insert split children into fifo while updating neighbour state
    int childPointsStartIdx = node0.start;
    for (int i = 0; i < 8; i++) {
      if (!childCounts[i]) {
        // child is empty: skip
        continue;
      }

      // create new child
      fifo.emplace_back();
      auto& child = fifo.back();

      int x = !!(i & 4);
      int y = !!(i & 2);
      int z = !!(i & 1);

      child.pos[0] = node0.pos[0] + (x << childSizeLog2);
      child.pos[1] = node0.pos[1] + (y << childSizeLog2);
      child.pos[2] = node0.pos[2] + (z << childSizeLog2);

      child.start = childPointsStartIdx;
      childPointsStartIdx += childCounts[i];
      child.end = childPointsStartIdx;
      child.numSiblingsPlus1 = numSiblings;
      child.siblingOccupancy = occupancy;

      bool idcmEnabled = _gps->inferred_direct_coding_mode_enabled_flag;
      if (isDirectModeEligible(idcmEnabled, nodeSizeLog2, node0, child)) {
        bool directModeUsed =
          encoder.encodeDirectPosition(childSizeLog2, child, pointCloud);

        if (directModeUsed) {
          // point reordering to match decoder's order
          for (auto idx = child.start; idx < child.end; idx++)
            pointIdxToDmIdx[idx] = nextDmIdx++;

          // NB: by definition, this is the only child node present
          assert(child.numSiblingsPlus1 == 1);

          // remove leaf node from fifo: it has been consumed and will
          // not be further split.
          fifo.pop_back();
          break;
        }
      }

      numNodesNextLvl++;

      // NB: when neighbourAvailBoundaryLog2 is set, an alternative
      //     implementation is used to calculate neighPattern.
      if (!_gps->neighbour_avail_boundary_log2) {
        updateGeometryNeighState(
          _gps->neighbour_context_restriction_flag, fifo.end(),
          numNodesNextLvl, childSizeLog2, child, i, node0.neighPattern,
          occupancy);
      }
    }
  }

  ////
  // The following is to re-order the points according in the decoding
  // order since IDCM causes leaves to be coded earlier than they
  // otherwise would.
  PCCPointSet3 pointCloud2;
  pointCloud2.addRemoveAttributes(
    pointCloud.hasColors(), pointCloud.hasReflectances());
  pointCloud2.resize(pointCloud.getPointCount());

  // copy points with DM points first, the rest second
  int outIdx = nextDmIdx;
  for (int i = 0; i < pointIdxToDmIdx.size(); i++) {
    int dstIdx = pointIdxToDmIdx[i];
    if (dstIdx == -1) {
      dstIdx = outIdx++;
    }

    pointCloud2[dstIdx] = pointCloud[i];
    if (pointCloud.hasColors())
      pointCloud2.setColor(dstIdx, pointCloud.getColor(i));
    if (pointCloud.hasReflectances())
      pointCloud2.setReflectance(dstIdx, pointCloud.getReflectance(i));
  }

  swap(pointCloud, pointCloud2);

  uint32_t acDataLen = arithmeticEncoder.stop_encoder();
  std::copy_n(arithmeticEncoder.buffer(), acDataLen, std::back_inserter(*buf));
}

//============================================================================

}  // namespace pcc