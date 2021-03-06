#include "TGeoManager.h"
#include "TVector3.h"
#include "TString.h"

#include "tpcrs/configurator.h"
#include "tpcrs/detail/coords.h"
#include "logger.h"
#include "math_funcs.h"

double mag(const Coords& c)
{
  return std::sqrt(c.x*c.x + c.y*c.y + c.z*c.z);
}

double perp(const Coords& c)
{
  return std::sqrt(c.x*c.x + c.y*c.y);
}

Coords unit(const Coords& c)
{
  double m = mag(c);
  return m ? Coords{c.x/m, c.y/m, c.z/m} : Coords{};
}

Coords operator-(const Coords &c1, const Coords &c2)
{
  return Coords{c1.x - c2.x, c1.y - c2.y, c1.z - c2.z};
}

double operator*(const Coords &c1, const Coords &c2)
{
  return c1.x*c2.x + c1.y*c2.y + c1.z*c2.z;
}

Coords operator/(const Coords &c, double v)
{
  return Coords{c.x/v, c.y/v, c.z/v};
}


CoordTransform::CoordTransform(const tpcrs::Configurator& cfg) :
  cfg_(cfg),
  timebin_width_ (1e6 / cfg_.S<starClockOnl>().frequency),
  z_inner_offset_(cfg_.S<tpcEffectiveGeom>().z_inner_offset),
  z_outer_offset_(cfg_.S<tpcEffectiveGeom>().z_outer_offset),
  tpc2global_("Tpc2Glob"),
  sector_rotations_(12*2*kTotalTpcSectorRotaions)
{
  SetTpcRotations();
}


// Local Sector Coordnate <-> Tpc Raw Pad Coordinate
void CoordTransform::local_sector_to_hardware(const StTpcLocalSectorCoordinate &a, StTpcPadCoordinate &b) const
{
  int row = a.row;

  if (row < 1 || row > cfg_.S<tpcPadPlanes>().padRows)
    row = YToRow(a.position.y, a.sector);

  double probablePad = XToPad(a.position.x, a.sector, a.row);
  double zoffset = (row > cfg_.S<tpcPadPlanes>().innerPadRows) ? z_outer_offset_ : z_inner_offset_;
  double tb = ZToTime(a.position.z + zoffset, a.sector, row, probablePad);

  b = StTpcPadCoordinate{a.sector, row, probablePad, tb};
}


void CoordTransform::hardware_to_local_sector(const StTpcPadCoordinate &a, StTpcLocalSectorCoordinate &b) const
{
  double x = PadToX(a.sector, a.row, a.pad);
  double y = tpcrs::RadialDistanceAtRow(a.row, cfg_);
  double zoffset =  (a.row > cfg_.S<tpcPadPlanes>().innerPadRows) ? z_outer_offset_ : z_inner_offset_;
  double z = TimeToZ(a.timeBucket, a.sector, a.row, a.pad) - zoffset;
  b = StTpcLocalSectorCoordinate{{x, y, z}, a.sector, a.row};
}


double CoordTransform::XToPad(double x, int sector, int row) const
{
  if (row > cfg_.S<tpcPadPlanes>().padRows) row = cfg_.S<tpcPadPlanes>().padRows;

  double pitch = (row <= cfg_.S<tpcPadPlanes>().innerPadRows) ?
                         cfg_.S<tpcPadPlanes>().innerSectorPadPitch :
                         cfg_.S<tpcPadPlanes>().outerSectorPadPitch;

  int npads = tpcrs::NumberOfPads(row, cfg_);
  double probablePad = (npads + 1.) / 2. - x / pitch;

  // CAUTION: pad cannot be <1
  return probablePad < 0.500001 ? 0.500001 : probablePad;
}


double CoordTransform::PadToX(int sector, int row, double pad) const      // x coordinate in sector 12
{
  if (row > cfg_.S<tpcPadPlanes>().padRows) row = cfg_.S<tpcPadPlanes>().padRows;

  double pitch = (row <= cfg_.S<tpcPadPlanes>().innerPadRows) ?
                         cfg_.S<tpcPadPlanes>().innerSectorPadPitch :
                         cfg_.S<tpcPadPlanes>().outerSectorPadPitch;

  int npads = tpcrs::NumberOfPads(row, cfg_);

  return -pitch * (pad - (npads + 1.) / 2.);
}


double CoordTransform::TimeToZ(double tb, int sector, int row, int pad) const
{
  if (row > cfg_.S<tpcPadPlanes>().padRows) row = cfg_.S<tpcPadPlanes>().padRows;

  // TODO: Remove extra temporary when new reference is introduced for tests
  float triggerTimeOffset = 1e-6 * cfg_.S<trgTimeOffset>().offset;
  double trigT0 = triggerTimeOffset * 1e6; // units are s
  double elecT0 = cfg_.S<tpcElectronics>().tZero;    // units are us
  double sectT0 = cfg_.S<tpcPadrowT0>(sector-1).T0[row-1];  // units are us
  double t0 = trigT0 + elecT0 + sectT0;
  int l = sector;

  if (tpcrs::IsInner(row, cfg_)) l += 24;

  double tbx = tb + cfg_.S<tpcSectorT0offset>().t0[l-1];
  double time = t0 + tbx * timebin_width_;

  return tpcrs::DriftVelocity(sector, cfg_) * 1e-6 * time;
}


double CoordTransform::ZToTime(double z, int sector, int row, int pad) const
{
  if (row > cfg_.S<tpcPadPlanes>().padRows) row = cfg_.S<tpcPadPlanes>().padRows;

  // TODO: Remove extra temporary when new reference is introduced for tests
  float triggerTimeOffset = 1e-6 * cfg_.S<trgTimeOffset>().offset;
  double trigT0 = triggerTimeOffset * 1e6; // units are s
  double elecT0 = cfg_.S<tpcElectronics>().tZero;    // units are us
  double sectT0 = cfg_.S<tpcPadrowT0>(sector-1).T0[row-1];  // units are us
  double t0 = trigT0 + elecT0 + sectT0;
  double time = z / (tpcrs::DriftVelocity(sector, cfg_) * 1e-6);
  int l = sector;

  if (tpcrs::IsInner(row, cfg_)) l += 24;

  return (time - t0) / timebin_width_ - cfg_.S<tpcSectorT0offset>().t0[l-1];
}


int CoordTransform::YToRow(double y, int sector) const
{
  static int Nrows = 0;
  static double* Radii = 0;

  if (Nrows != cfg_.S<tpcPadPlanes>().padRows) {
    Nrows = cfg_.S<tpcPadPlanes>().padRows;

    if (Radii) delete [] Radii;

    Radii = new double[Nrows + 1];

    for (int i = 1; i <= Nrows + 1; i++) {
      if (i == 1) {
        Radii[i - 1] =  (3 * tpcrs::RadialDistanceAtRow(i, cfg_)
                           - tpcrs::RadialDistanceAtRow(i + 1, cfg_)) / 2;
      }
      else if (i == Nrows + 1) {
        Radii[i - 1] =  (3 * tpcrs::RadialDistanceAtRow(i - 1, cfg_)
                           - tpcrs::RadialDistanceAtRow(i - 2, cfg_)) / 2;
      }
      else {
        Radii[i - 1] = (tpcrs::RadialDistanceAtRow(i - 1, cfg_) +
                        tpcrs::RadialDistanceAtRow(i, cfg_)) / 2;
      }
    }
  }

  // Based on the row position (i.e. "radius") perform a binary search for the
  // row corresponding to the value of y
  double* r_ptr = std::lower_bound(Radii, Radii + Nrows + 1, y);
  int row = (r_ptr != Radii + Nrows + 1) && (*r_ptr == y) ? r_ptr - Radii + 1: r_ptr - Radii;

  if (row <= 0) row = 1;
  if (row > Nrows) row = Nrows;

  return row;
}


void CoordTransform::local_sector_to_local(const StTpcLocalSectorCoordinate &a, StTpcLocalCoordinate &b) const
{
  int row = a.row;

  if (row < 1 || row > cfg_.S<tpcPadPlanes>().padRows)
    row = YToRow(a.position.y, a.sector);

  Coords xGG;
  Pad2Tpc(a.sector, row).LocalToMasterVect(a.position.xyz(), xGG.xyz());
  const double* trans = Pad2Tpc(a.sector, row).GetTranslation();
  TGeoTranslation GG2TPC(trans[0], trans[1], trans[2]);
  GG2TPC.LocalToMaster(xGG.xyz(), b.position.xyz());

  b.row = row;
  b.sector = a.sector;
}


void CoordTransform::local_to_local_sector(const StTpcLocalCoordinate &a, StTpcLocalSectorCoordinate &b) const
{
  int row = a.row;

  if (row < 1 || row > cfg_.S<tpcPadPlanes>().padRows) {
    Coords xyzS;
    SupS2Tpc(a.sector).MasterToLocalVect(a.position.xyz(), xyzS.xyz());
    row = YToRow(xyzS.x, a.sector);
  }

  const double* trans = Pad2Tpc(a.sector, row).GetTranslation();
  TGeoTranslation GG2TPC(trans[0], trans[1], trans[2]);
  Coords xGG;
  GG2TPC.MasterToLocal(a.position.xyz(), xGG.xyz());
  Pad2Tpc(a.sector, row).MasterToLocalVect(xGG.xyz(), b.position.xyz());

  b.row = row;
  b.sector = a.sector;
}


void CoordTransform::SetTpcRotations()
{
  // Pad [== sector12 == localsector (SecL, ideal)] => subsector (SubS,local sector aligned) => flip => sector (SupS) => tpc => global
  //                                    ------
  // old:
  //  global = tpc2global_ * SupS2Tpc(sector) *                                    flip_matrix * {SubSInner2SupS(sector) | SubSOuter2SupS(sector)}
  // new:
  //  global = tpc2global_ * SupS2Tpc(sector) * StTpcSuperSectorPosition(sector) * flip_matrix * {                     I | StTpcOuterSectorPosition(sector)}
  //
  //      StTpcSuperSectorPosition(sector) * flip_matrix = flip_matrix * SubSInner2SupS(sector)
  //  =>  StTpcSuperSectorPosition(sector) = flip_matrix * SubSInner2SupS(sector) * flip_matri1
  //
  //      StTpcSuperSectorPosition(sector) * flip_matrix * StTpcOuterSectorPosition(sector) =    flip_matrix * SubSOuter2SupS(sector)
  //  =>  StTpcOuterSectorPosition(sector) = flip_matri1 * StTpcSuperSectorPosition(sector)^-1 * flip_matrix * SubSOuter2SupS(sector)
  //
  //         <-- the system of coordinate where Outer to Inner Alignment done -->
  //
  //  global = tpc2global_ * SupS2Tpc(sector) * StTpcSuperSectorPosition(sector) * flip_matrix * {                     I | StTpcOuterSectorPosition(sector)} * local
  //                         result of super sector alignment                                      result of Outer to Inner sub sector alignment
  //
  // Additonal note:
  //
  //  StTpcPadCoordinate P(sector,row,pad,timebacket);
  //  x = PadToX()
  //  z = TimeToZ() - drift distance
  //  StTpcLocalSectorCoordinate LS(position(x,y,z),sector,row);
  //
  //  StTpcLocalCoordinate  Tpc(position(x,y,z),sector,row);
  //  Pad2Tpc(sector,row).LocalToMaster(LS.position().xyz(), Tpc.postion().xyz())
  //  Flip transformation from Pad Coordinate system
  //    (PadToX(pad), RadialDistanceAtRow(row), DriftDistance(timebacket)) => (y, x, -z) : local sector CS => super sectoe CS
  //
  //          (0 1  0 0  ) ( x )    ( y )
  //  Flip ;  (1 0  0 0  ) ( y ) =  ( x )
  //          (0 0 -1 zGG) ( z )    ( zGG - z)
  //          (0 0  0 1  ) ( 1 )    ( 1 )
  //
  // Z_tpc is not changed during any sector transformation!!!

  TGeoHMatrix flip_matrix("flip");
  double Rotation[9] = {0, 1, 0,
                        1, 0, 0,
                        0, 0, -1};
  flip_matrix.SetRotation(Rotation);

  for (int sector = 0; sector <= 24; sector++) {// loop over Tpc as whole, sectors, inner and outer subsectors
    int k;
    int k1 = kSupS2Tpc;
    int k2 = kTotalTpcSectorRotaions;

    if (sector == 0) {k2 = k1; k1 = kUndefSector;}

    for (k = k1; k < k2; k++)
    {
      TGeoHMatrix rotA; // After alignment

      if (sector == 0) { // TPC Reference System
        double phi   = 0.0;                                           // -gamma large uncertainty, so set to 0
        double theta = cfg_.S<tpcGlobalPosition>().PhiXZ_geom * 180.0 / M_PI; // -beta
        double psi   = cfg_.S<tpcGlobalPosition>().PhiYZ_geom * 180.0 / M_PI; // -alpha
        rotA.RotateX(-psi);
        rotA.RotateY(-theta);
        rotA.RotateZ(-phi);
        double transTpcRefSys[3] = {cfg_.S<tpcGlobalPosition>().LocalxShift,
                                    cfg_.S<tpcGlobalPosition>().LocalyShift,
                                    cfg_.S<tpcGlobalPosition>().LocalzShift };
        rotA.SetTranslation(transTpcRefSys);
      }
      else {

        switch (k)
        {
        case kSupS2Tpc: // SupS => Tpc
        {
          // Rotation around x, y, and z axes
          TGeoRotation rotm("temp_matrix");
          // Sector phi angle
          int iphi = (360 + 90 - 30 * sector) % 360;
          // Account for signed drift distance along z
          double drift_dist_z = cfg_.S<tpcPadPlanes>().outerSectorPadPlaneZ -
                                cfg_.S<tpcWirePlanes>().outerSectorGatingGridPadSep;
          if (sector > 12) {
            rotm.SetAngles(90.0,    0.0,  90.0,  -90.0,  180.0,    0.00);// Flip (x,y,z) => ( x,-y,-z)
            drift_dist_z *= -1;
          //iphi = (360 + 90 - 30 *  sector      ) % 360;
            iphi = (      90 + 30 * (sector - 12)) % 360;
          }

          rotm.RotateZ(iphi);

          rotA = TGeoTranslation(0, 0, drift_dist_z) * rotm;
          rotA *= cfg_.C<StTpcSuperSectorPosition>().GetMatrix(sector - 1);
          break;
        }

        case kSupS2Glob:      // SupS => Tpc => Glob
          rotA = tpc2global_ * SupS2Tpc(sector);
          break;

        case kSubSInner2SupS:
          rotA = flip_matrix;
          break;

        case kSubSOuter2SupS:
          rotA = flip_matrix * cfg_.C<StTpcOuterSectorPosition>().GetMatrix(sector - 1);
          break;

        // (Subs[io] => SupS) => Tpc
        case kSubSInner2Tpc:  rotA = SupS2Tpc(sector) * SubSInner2SupS(sector); break;
        case kSubSOuter2Tpc:  rotA = SupS2Tpc(sector) * SubSOuter2SupS(sector); break;
        // Subs[io] => SupS => Tpc) => Glob
        case kSubSInner2Glob: rotA = tpc2global_ * SubSInner2Tpc(sector);  break;
        case kSubSOuter2Glob: rotA = tpc2global_ * SubSOuter2Tpc(sector);  break;
        // (Pad == SecL) => (SubS[io] => SupS)
        case kPadInner2SupS:  rotA = SubSInner2SupS(sector); break;
        case kPadOuter2SupS:  rotA = SubSOuter2SupS(sector); break;
        // (Pad == SecL) => (SubS[io] => SupS => Tpc)
        case kPadInner2Tpc:   rotA = SupS2Tpc(sector) * PadInner2SupS(sector); break;
        case kPadOuter2Tpc:   rotA = SupS2Tpc(sector) * PadOuter2SupS(sector); break;
        // (Pad == SecL) => (SubS[io] => SupS => Tpc => Glob)
        case kPadInner2Glob:  rotA = tpc2global_ * PadInner2Tpc(sector); break;
        case kPadOuter2Glob:  rotA = tpc2global_ * PadOuter2Tpc(sector); break;

        default:
          assert(0);
        }
      }

      // Normalize
      double* r = rotA.GetRotationMatrix();

      TVector3 d(r[0], r[3], r[6]);
      TVector3 t(r[2], r[5], r[8]);
      TVector3 n(r[1], r[4], r[7]);

      d *= 1. / d.Mag();
      t *= 1. / t.Mag();

      TVector3 c = d.Cross(t);

      if (c.Dot(n) < 0) c *= -1;

      double rot[9] = {
        d[0], c[0], t[0],
        d[1], c[1], t[1],
        d[2], c[2], t[2]
      };

      rotA.SetRotation(rot);

      const char* names[kTotalTpcSectorRotaions] = {
        "SupS_%02itoTpc",
        "SupS_%02itoGlob",
        "SubS_%02iInner2SupS",
        "SubS_%02iOuter2SupS",
        "SubS_%02iInner2Tpc",
        "SubS_%02iOuter2Tpc",
        "SubS_%02iInner2Glob",
        "SubS_%02iOuter2Glob",
        "PadInner2SupS_%02i",
        "PadOuter2SupS_%02i",
        "SupS_%02i12Inner2Tpc",
        "SupS_%02i12Outer2Tpc",
        "SupS_%02i12Inner2Glob",
        "SupS_%02i12Outer2Glob"
      };

      if (sector == 0) {
        rotA.SetName("Tpc2Glob");
        tpc2global_ = rotA;
      }
      else {
        rotA.SetName(Form(names[k], sector));
        sector_rotations_[kTotalTpcSectorRotaions*(sector - 1) + k] = rotA;
      }
    }
  }
}
