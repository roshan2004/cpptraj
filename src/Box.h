#ifndef INC_BOX_H
#define INC_BOX_H
/// Hold box information.
class Box {
  public:
    enum BoxType { NOBOX=0, ORTHO, TRUNCOCT, RHOMBIC, NONORTHO }; 

    Box();
    Box(const double*);
    Box(const Box&);
    Box& operator=(const Box&);

    const char* TypeName() const; 

    void SetBetaLengths(double,double,double,double);
    void SetBox(const double*);
    void SetTruncOct();
    void SetNoBox();
    void SetMissingInfo(const Box&);

    double ToRecip(double*,double*);

    void SetX(double xin)     { box_[0] = xin; }
    void SetY(double yin)     { box_[1] = yin; }
    void SetZ(double zin)     { box_[2] = zin; }
    void SetAlpha(double ain) { box_[3] = ain; }
    void SetBeta(double bin)  { box_[4] = bin; }
    void SetGamma(double gin) { box_[5] = gin; }

    BoxType Type() const { return btype_;  }
    double BoxX()  const { return box_[0]; }
    double BoxY()  const { return box_[1]; }
    double BoxZ()  const { return box_[2]; }
    double Alpha() const { return box_[3]; }
    double Beta()  const { return box_[4]; }
    double Gamma() const { return box_[5]; }
    bool HasBox()  const { return (btype_ != NOBOX); }
  private:
    static const double TRUNCOCTBETA;
    static const char* BoxNames[];
    //int debug_; // TODO: Replace with ifdefs or just comment out?
    BoxType btype_;
    double box_[6];

    void SetBoxType();
};
#endif
