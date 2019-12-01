/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>frame
 *
 *F
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 *  2017 Jacques Desmis <jdesmis@gmail.com>
 *  2018 Pierre Cabrera <pierre.cab@gmail.com>
 */

#include "locallab.h"
#include "rtimage.h"
#include <iomanip>
#include "../rtengine/rt_math.h"
#include "options.h"
#include <cmath>
#include "editcallbacks.h"
#include "guiutils.h"
#include <string>
#include <unistd.h>
#include "../rtengine/color.h"
#include "../rtengine/improcfun.h"
#include "labgrid.h"

#define MINCHRO 0.
#define MAXCHRO 150.
#define MAXCHROCC 100.
#define MINNEIGH 0.1
#define MAXNEIGH 1500
#define CENTERNEIGH 200

using namespace rtengine;

extern Options options;
static double retiSlider2neigh(double sval)
{

    // slider range: 0 - 5000
    double neigh;

    if (sval <= 1000) {
        // linear below center-temp
        neigh = MINNEIGH + (sval / 1000.0) * (CENTERNEIGH - MINNEIGH);
    } else {
        const double slope = (double)(CENTERNEIGH - MINNEIGH) / (MAXNEIGH - CENTERNEIGH);
        double x = (sval - 1000) / 1000; // x 0..1
        double y = x * slope + (1.0 - slope) * pow(x, 4.0);
        neigh = CENTERNEIGH + y * (MAXNEIGH - CENTERNEIGH);
    }

    if (neigh < MINNEIGH) {
        neigh = MINNEIGH;
    }

    if (neigh > MAXNEIGH) {
        neigh = MAXNEIGH;
    }

    return neigh;
}

static double retiNeigh2Slider(double neigh)
{

    double sval;

    if (neigh <= CENTERNEIGH) {
        sval = ((neigh - MINNEIGH) / (CENTERNEIGH - MINNEIGH)) * 1000.0;
    } else {
        const double slope = (double)(CENTERNEIGH - MINNEIGH) / (MAXNEIGH - CENTERNEIGH);
        const double y = (neigh - CENTERNEIGH) / (MAXNEIGH - CENTERNEIGH);
        double x = pow(y, 0.25); // rough guess of x, will be a little lower
        double k = 0.1;
        bool add = true;

        // the y=f(x) function is a mess to invert, therefore we have this trial-refinement loop instead.
        // from tests, worst case is about 20 iterations, ie no problem
        for (;;) {
            double y1 = x * slope + (1.0 - slope) * pow(x, 4.0);

            if (1000 * fabs(y1 - y) < 0.1) {
                break;
            }

            if (y1 < y) {
                if (!add) {
                    k /= 2;
                }

                x += k;
                add = true;
            } else {
                if (add) {
                    k /= 2;
                }

                x -= k;
                add = false;
            }
        }

        sval = 1000.0 + x * 1000.0;
    }

    if (sval < 0) {
        sval = 0;
    }

    if (sval > 5000) {
        sval = 5000;
    }

    return sval;
}

Locallab::Locallab():
    FoldableToolPanel(this, "locallab", M("TP_LOCALLAB_LABEL"), false, true),

    // Expander widgets
    expsettings(Gtk::manage(new ControlSpotPanel())),
    expcolor(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_COFR")))),
    expexpose(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_EXPOSE")))),
    expshadhigh(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_SHADHIGH")))),
    expvibrance(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_VIBRANCE")))),
    expsoft(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_SOFT")))),
    expblur(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    exptonemap(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    expreti(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    expsharp(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    expcontrast(Gtk::manage(new MyExpander(true, M("TP_LOCALLAB_LOC_CONTRAST")))),
    expcbdl(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    expdenoi(Gtk::manage(new MyExpander(true, Gtk::manage(new Gtk::HBox())))),
    expmaskcol(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWC")))),
    expmaskcol1(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWC1")))),
    expmaskexp(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWE")))),
    expmasksh(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWS")))),
    expmaskcb(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWCB")))),
    expmaskreti(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWR")))),
    expmasktm(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWT")))),
    expmaskbl(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWPLUS")))),
    expmaskvib(Gtk::manage(new MyExpander(false, M("TP_LOCALLAB_SHOWVI")))),


    // CurveEditorGroup widgets
    // Color & Light
    llCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_LUM"))),
    clCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_CH"))),
    HCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_HLH"))),
    H2CurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_HLH"))),
    rgbCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_RGB"))),
    maskCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASKCOL"))),
    maskHCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASKH"))),
    mask2CurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),
    mask2CurveEditorGwav(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_WAVMASK"))),

    Lmaskshape(static_cast<DiagonalCurveEditor*>(mask2CurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    llshape(static_cast<DiagonalCurveEditor*>(llCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    ccshape(static_cast<DiagonalCurveEditor*>(llCurveEditorG->addCurve(CT_Diagonal, "C(C)"))),
    clshape(static_cast<DiagonalCurveEditor*>(clCurveEditorG->addCurve(CT_Diagonal, "C(L)"))),
    lcshape(static_cast<DiagonalCurveEditor*>(clCurveEditorG->addCurve(CT_Diagonal, "L(C)"))),
    toneMethod(Gtk::manage(new MyComboBoxText())),
    rgbshape(static_cast<DiagonalCurveEditor*>(rgbCurveEditorG->addCurve(CT_Diagonal, "", toneMethod))),
    LHshape(static_cast<FlatCurveEditor*>(HCurveEditorG->addCurve(CT_Flat, "L(H)", nullptr, false, true))),
    HHshape(static_cast<FlatCurveEditor*>(H2CurveEditorG->addCurve(CT_Flat, "H(H)", nullptr, false, true))),
    CCmaskshape(static_cast<FlatCurveEditor*>(maskCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskshape(static_cast<FlatCurveEditor*>(maskCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskshape(static_cast<FlatCurveEditor *>(maskCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),
    LLmaskcolshapewav(static_cast<FlatCurveEditor*>(mask2CurveEditorGwav->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHhmaskshape(static_cast<FlatCurveEditor *>(maskHCurveEditorG->addCurve(CT_Flat, "H(H)", nullptr, false, true))),

    // Exposure
    curveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_CURVEEDITOR_TONES_LABEL"))),
    maskexpCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2expCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),

    Lmaskexpshape(static_cast<DiagonalCurveEditor*>(mask2expCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    shapeexpos(static_cast<DiagonalCurveEditor*>(curveEditorG->addCurve(CT_Diagonal, ""))),
    CCmaskexpshape(static_cast<FlatCurveEditor*>(maskexpCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskexpshape(static_cast<FlatCurveEditor*>(maskexpCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskexpshape(static_cast<FlatCurveEditor *>(maskexpCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),

    //Shadows Highlight
    maskSHCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2SHCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),

    LmaskSHshape(static_cast<DiagonalCurveEditor*>(mask2SHCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    CCmaskSHshape(static_cast<FlatCurveEditor*>(maskSHCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskSHshape(static_cast<FlatCurveEditor*>(maskSHCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskSHshape(static_cast<FlatCurveEditor *>(maskSHCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),

    // Vibrance
    curveEditorGG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_LABEL"))),
    maskvibCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    skinTonesCurve(static_cast<DiagonalCurveEditor*>(curveEditorGG->addCurve(CT_Diagonal, M("TP_VIBRANCE_CURVEEDITOR_SKINTONES")))),
    mask2vibCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),
    Lmaskvibshape(static_cast<DiagonalCurveEditor*>(mask2vibCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    CCmaskvibshape(static_cast<FlatCurveEditor*>(maskvibCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskvibshape(static_cast<FlatCurveEditor*>(maskvibCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskvibshape(static_cast<FlatCurveEditor *>(maskvibCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),


    //Blur
    maskblCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2blCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2blCurveEditorGwav(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_WAVMASK"))),

    Lmaskblshape(static_cast<DiagonalCurveEditor*>(mask2blCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    CCmaskblshape(static_cast<FlatCurveEditor*>(maskblCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskblshape(static_cast<FlatCurveEditor*>(maskblCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskblshape(static_cast<FlatCurveEditor *>(maskblCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),
    LLmaskblshapewav(static_cast<FlatCurveEditor*>(mask2blCurveEditorGwav->addCurve(CT_Flat, "L(L)", nullptr, false, false))),

    //TM
    masktmCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2tmCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),

    Lmasktmshape(static_cast<DiagonalCurveEditor*>(mask2tmCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    CCmasktmshape(static_cast<FlatCurveEditor*>(masktmCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmasktmshape(static_cast<FlatCurveEditor*>(masktmCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmasktmshape(static_cast<FlatCurveEditor *>(masktmCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),

// Retinex
    LocalcurveEditortransT(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_TRANSMISSIONMAP"))),
    LocalcurveEditorgainT(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_TRANSMISSIONGAIN"))),
    maskretiCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2retiCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),

    Lmaskretishape(static_cast<DiagonalCurveEditor*>(mask2retiCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    cTtransshape(static_cast<FlatCurveEditor*>(LocalcurveEditortransT->addCurve(CT_Flat, "", nullptr, false, false))),
    cTgainshape(static_cast<FlatCurveEditor*>(LocalcurveEditorgainT->addCurve(CT_Flat, "", nullptr, false, false))),
    CCmaskretishape(static_cast<FlatCurveEditor*>(maskretiCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskretishape(static_cast<FlatCurveEditor*>(maskretiCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskretishape(static_cast<FlatCurveEditor *>(maskretiCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),

//Local contrast
    LocalcurveEditorwav(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_WAV"))),

    wavshape(static_cast<FlatCurveEditor*>(LocalcurveEditorwav->addCurve(CT_Flat, "", nullptr, false, false))),

    //CBDL
    maskcbCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK"))),
    mask2cbCurveEditorG(new CurveEditorGroup(options.lastlocalCurvesDir, M("TP_LOCALLAB_MASK2"))),

    Lmaskcbshape(static_cast<DiagonalCurveEditor*>(mask2cbCurveEditorG->addCurve(CT_Diagonal, "L(L)"))),
    CCmaskcbshape(static_cast<FlatCurveEditor*>(maskcbCurveEditorG->addCurve(CT_Flat, "C(C)", nullptr, false, false))),
    LLmaskcbshape(static_cast<FlatCurveEditor*>(maskcbCurveEditorG->addCurve(CT_Flat, "L(L)", nullptr, false, false))),
    HHmaskcbshape(static_cast<FlatCurveEditor *>(maskcbCurveEditorG->addCurve(CT_Flat, "LC(H)", nullptr, false, true))),

    // Adjuster widgets
    // Color & Light
    lightness(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LIGHTNESS"), -100, 500, 1, 0))),
    contrast(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CONTRAST"), -100, 100, 1, 0))),
    chroma(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMA"), -100, 150, 1, 0))),
    strengthgrid(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRGRID"), 0, 100, 1, 30))),
    sensi(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 0, 100, 1, 15))),
    structcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRUCCOL1"), 0, 100, 1, 0))),
    blurcolde(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLURDE"), 2, 100, 1, 5))),
    strcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTRLUM"), -4., 4., 0.05, 0.))),
    strcolab(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTRCHRO"), -6., 6., 0.05, 0.))),
    strcolh(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTRHUE"), -6., 6., 0.05, 0.))),
    angcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADANG"), -180, 180, 0.1, 0.))),
    blendmaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
    radmaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
    chromaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
    gammaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.25, 4.0, 0.01, 1.))),
    slomaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
    lapmaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
    shadmaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHAMASKCOL"), 0, 100, 1, 0))),
    softradiuscol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SOFTRADIUSCOL"), -10.0, 1000.0, 0.5, 0.))),
    opacol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_OPACOL"), 0.0, 100.0, 0.5, 60.))),
    conthrcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CONTTHR"), 0.0, 100.0, 0.5, 0.))),
    strumaskcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRUMASKCOL"), 0., 200., 0.1, 0.))),
    mercol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_MERDCOL"), 0.0, 100.0, 0.5, 18.))),
    merlucol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_MERLUCOL"), 0.0, 100.0, 0.5, 32., Gtk::manage(new RTImage("circle-black-small.png")), Gtk::manage(new RTImage("circle-white-small.png"))))),
    blurcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLURCOL"), 0.2, 100., 0.5, 0.2))),
    contcol(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CONTCOL"), 0., 200., 0.5, 0.))),
    // Exposure
    expcomp(Gtk::manage(new Adjuster(M("TP_EXPOSURE_EXPCOMP"), -2.0, 3.0, 0.05, 0.0))),
    hlcompr(Gtk::manage(new Adjuster(M("TP_EXPOSURE_COMPRHIGHLIGHTS"), 0, 500, 1, 0))),
    hlcomprthresh(Gtk::manage(new Adjuster(M("TP_EXPOSURE_COMPRHIGHLIGHTSTHRESHOLD"), 0, 100, 1, 0))),
    black(Gtk::manage(new Adjuster(M("TP_EXPOSURE_BLACKLEVEL"), -16384, 32768, 10, 0))),
    shadex(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHADEX"), 0, 100, 1, 0))),
    shcompr(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHADEXCOMP"), 0, 100, 1, 50))),
    expchroma(Gtk::manage(new Adjuster(M("TP_LOCALLAB_EXPCHROMA"), -50, 100, 1, 30))),
    warm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_WARM"), -100., 100., 1., 0., Gtk::manage(new RTImage("circle-blue-small.png")), Gtk::manage(new RTImage("circle-orange-small.png"))))),
    sensiex(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 0, 100, 1, 15))),
    structexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRUCCOL"), 0, 100, 1, 0))),
    blurexpde(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLURDE"), 2, 100, 1, 5))),
    strexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTR"), -4., 4., 0.05, 0.))),
    angexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADANG"), -180, 180, 0.1, 0.))),
    blendmaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
    radmaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
    chromaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
    gammaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.25, 4.0, 0.01, 1.))),
    slomaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
    lapmaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
    softradiusexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SOFTRADIUSCOL"), -10.0, 1000.0, 0.5, 0.))),
    laplacexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPLACEXP"), 0.0, 100.0, 0.1, 0.))),
    balanexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BALANEXP"), 0.2, 1.2, 0.01, 0.75))),
    linear(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LINEAR"), 0., 1., 0.01, 0.3))),
    gamm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMM"), 0.2, 1.3, 0.01, 0.4))),
    fatamount(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATAMOUNT"), 1., 100., 1., 1.))),
    fatdetail(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATDETAIL"), -100., 300., 1., 0.))),
    fatanchor(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATANCHOR"), 1., 100., 1., 50., Gtk::manage(new RTImage("circle-black-small.png")), Gtk::manage(new RTImage("circle-white-small.png"))))),
    strmaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTR"), -2., 2., 0.05, 0.))),
    angmaskexp(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADANG"), -180., 180., 0.1, 0.))),
    fatlevel(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATLEVEL"), 0, 3, 1, 2))),
    multipliersh(
        [this]() -> std::array<Adjuster *, 5>
{
    std::array<Adjuster*, 5> res = {};

    for (unsigned int i = 0; i < res.size(); ++i) {
        Glib::ustring ss = Glib::ustring::format(i);

        if (i == 0) {
            ss += Glib::ustring::compose(" (%1)", M("TP_LOCALLAB_LUMADARKEST"));
        } else if (i == 4) {
            ss += Glib::ustring::compose(" (%1)", M("TP_LOCALLAB_LUMAWHITESEST"));
        }

        res[i] = Gtk::manage(new Adjuster(std::move(ss), -100, 100, 1, 0));
        res[i]->setAdjusterListener(this);
    }

    return res;
}
()
),
//Shadow hightlights
highlights(Gtk::manage(new Adjuster(M("TP_SHADOWSHLIGHTS_HIGHLIGHTS"), 0, 100, 1, 0))),
h_tonalwidth(Gtk::manage(new Adjuster(M("TP_SHADOWSHLIGHTS_HLTONALW"), 10, 100, 1, 70))),
shadows(Gtk::manage(new Adjuster(M("TP_SHADOWSHLIGHTS_SHADOWS"), 0, 100, 1, 0))),
s_tonalwidth(Gtk::manage(new Adjuster(M("TP_SHADOWSHLIGHTS_SHTONALW"), 10, 100, 1, 30))),
sh_radius(Gtk::manage(new Adjuster(M("TP_SHADOWSHLIGHTS_RADIUS"), 0, 100, 1, 40))),
sensihs(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 0, 100, 1, 15))),
blendmaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
blurSHde(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLURDE"), 2, 100, 1, 5))),
strSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTR"), -4., 4., 0.05, 0.))),
angSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADANG"), -180, 180, 0.1, 0.))),
chromaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.25, 4.0, 0.01, 1.))),
slomaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmaskSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
detailSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_DETAILSH"), -5, 5, 1, 0))),
fatamountSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATAMOUNT"), 1., 100., 1., 1.))),
fatanchorSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_FATANCHOR"), 1., 100., 1., 50., Gtk::manage(new RTImage("circle-black-small.png")), Gtk::manage(new RTImage("circle-white-small.png"))))),
gamSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMSH"), 0.25, 15.0, 0.01, 2.4))),
sloSH(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOSH"), 0.0, 100.0, 0.01, 12.92))),
// Vibrance
saturated(Gtk::manage(new Adjuster(M("TP_VIBRANCE_SATURATED"), -100., 100., 1., 0.))),
pastels(Gtk::manage(new Adjuster(M("TP_VIBRANCE_PASTELS"), -100., 100., 1., 0.))),
sensiv(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 0, 100, 1, 15))),
blendmaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
chromaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.25, 4.0, 0.01, 1.))),
slomaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmaskvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
strvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTR"), -4., 4., 0.05, 0.))),
strvibab(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTRCHRO"), -4., 4., 0.05, 0.))),
strvibh(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADSTRHUE2"), -6., 6., 0.05, 0.))),
angvib(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GRADANG"), -180, 180, 0.1, 0.))),
//Soft Light
streng(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRENG"), 1, 100, 1, 1))),
laplace(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPLACE"), 0., 100., 0.5, 25.))),
sensisf(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 1, 100, 1, 15))),
// Blur & Noise
radius(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADIUS"), 1.5, 1000.0, 0.1, 1.5))),
strength(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRENGTH"), 0, 100, 1, 0))),
itera(Gtk::manage(new Adjuster(M("TP_DIRPYRDENOISE_MEDIAN_PASSES"), 1, 4, 1, 1))),
guidbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GUIDBL"), 0, 1000, 1, 0))),
epsbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_EPSBL"), -10, 10, 1, 0))),
sensibn(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSIBN"), 0, 100, 1, 40))),
blendmaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
chromaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.05, 5.0, 0.01, 1.))),
slomaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
shadmaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_HIGHMASKCOL"), 0, 100, 1, 0))),
isogr(Gtk::manage(new Adjuster(M("TP_LOCALLAB_ISOGR"), 20, 6400, 1, 400))),
strengr(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRENGR"), 0, 100, 1, 0))),
scalegr(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SCALEGR"), 0, 100, 1, 100))),
strumaskbl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STRUMASKCOL"), 0., 200., 0.1, 0.))),
// Tone Mapping
stren(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STREN"), -0.5, 2.0, 0.01, 0.5))),
gamma(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAM"), 0.4, 4.0, 0.11, 1.0))),
estop(Gtk::manage(new Adjuster(M("TP_LOCALLAB_ESTOP"), 0.1, 4., 0.01, 1.4))),
scaltm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SCALTM"), 0.1, 10.0, 0.01, 1.0))),
rewei(Gtk::manage(new Adjuster(M("TP_LOCALLAB_REWEI"), 0, 3, 1, 0))),
sensitm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSI"), 0, 100, 1, 15))),
softradiustm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SOFTRADIUSCOL"), -10.0, 1000.0, 0.1, 0.))),
amount(Gtk::manage(new Adjuster(M("TP_LOCALLAB_AMOUNT"), 50., 100.0, 0.5, 95.))),
satur(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SATUR"), -100., 100., 0.1, 0.))),//by default satur = 0 ==> use Mantiuk value
blendmasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
chromasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.05, 5.0, 0.01, 1.))),
slomasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmasktm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
// Retinex
str(Gtk::manage(new Adjuster(M("TP_LOCALLAB_STR"), 0., 100., 0.1, 0.2))),
chrrt(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHRRT"), 0.0, 100.0, 0.1, 0.0))),
neigh(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NEIGH"), MINNEIGH, MAXNEIGH, 0.5, 50., nullptr, nullptr, &retiSlider2neigh, &retiNeigh2Slider))),
vart(Gtk::manage(new Adjuster(M("TP_LOCALLAB_VART"), 0.1, 500., 0.1, 150.))),
offs(Gtk::manage(new Adjuster(M("TP_LOCALLAB_OFFS"), -16386., 32768., 1., 0.))),
dehaz(Gtk::manage(new Adjuster(M("TP_LOCALLAB_DEHAZ"), 0, 100, 1, 0))),
depth(Gtk::manage(new Adjuster(M("TP_LOCALLAB_DEPTH"), 0, 100, 1, 25))),
sensih(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSIH"), 0, 100, 1, 60))),
softradiusret(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SOFTRETI"), -10.0, 1000.0, 0.5, 40.))),
blendmaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 10.))),
chromaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.05, 5.0, 0.01, 1.))),
slomaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmaskreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
scalereti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SCALERETI"), 1.0, 10.0, 1., 2.))),
darkness(Gtk::manage(new Adjuster(M("TP_LOCALLAB_DARKRETI"), 0.01, 6.0, 0.01, 2.0))),
lightnessreti(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LIGHTRETI"), 0.01, 4.0, 0.01, 1.))),
limd(Gtk::manage(new Adjuster(M("TP_LOCALLAB_THRESRETI"), 1.2, 100.0, 0.1, 8.))),
cliptm(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CLIPTM"), 0.02, 2.0, 0.01, 1.))),
// Sharpening
sharcontrast(Gtk::manage(new Adjuster(M("TP_SHARPENING_CONTRAST"), 0, 200, 1, 20))),
sharradius(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHARRADIUS"), 0.4, 2.5, 0.01, 0.75))),
sharamount(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHARAMOUNT"), 0, 100, 1, 100))),
shardamping(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHARDAMPING"), 0, 100, 1, 0))),
shariter(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHARITER"), 5, 100, 1, 30))),
sharblur(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SHARBLUR"), 0.2, 2.0, 0.05, 0.2))),
sensisha(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSIS"), 0, 100, 1, 19))),
// Local Contrast
lcradius(Gtk::manage(new Adjuster(M("TP_LOCALCONTRAST_RADIUS"), 20, 400, 1, 80))),
lcamount(Gtk::manage(new Adjuster(M("TP_LOCALCONTRAST_AMOUNT"), 0, 1.0, 0.01, 0))),
lcdarkness(Gtk::manage(new Adjuster(M("TP_LOCALCONTRAST_DARKNESS"), 0, 3.0, 0.01, 1.0))),
lclightness(Gtk::manage(new Adjuster(M("TP_LOCALCONTRAST_LIGHTNESS"), 0, 3.0, 0.01, 1.0))),
levelwav(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LEVELWAV"), 1, 9, 1, 4))),
residcont(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RESIDCONT"), -100, 100, 1, 0))),
clarilres(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CLARILRES"), -20., 100., 0.5, 0.))),
clarisoft(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CLARISOFT"), 0., 100., 0.5, 0.))),
claricres(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CLARICRES"), -20., 100., 0.5, 0.))),
sensilc(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSIS"), 0, 100, 1, 19))),
residchro(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RESIDCHRO"), -100, 100, 1, 0))),
multiplier(
[this]() -> std::array<Adjuster*, 6> {
    std::array<Adjuster*, 6> res = {};

    for (unsigned int i = 0; i < res.size(); ++i)
    {
        Glib::ustring ss = Glib::ustring::format(i);

        if (i == 0) {
            ss += Glib::ustring::compose(" (%1)", M("TP_DIRPYREQUALIZER_LUMAFINEST"));
        } else if (i == 5) {
            ss += Glib::ustring::compose(" (%1)", M("TP_DIRPYREQUALIZER_LUMACOARSEST"));
        }

        res[i] = Gtk::manage(new Adjuster(std::move(ss), 0.0, 4.0, 0.01, 1.0));
        res[i]->setAdjusterListener(this);
    }
    return res;
}()
),
// Contrast by detail levels
chromacbdl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMACBDL"), 0., 1.5, 0.01, 0.))),
threshold(Gtk::manage(new Adjuster(M("TP_DIRPYREQUALIZER_THRESHOLD"), 0, 1., 0.01, 0.2))),
clarityml(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CLARITYML"), 0.1, 100., 0.1, 0.1))),
contresid(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CONTRESID"), -100, 100, 1, 0))),
blurcbdl(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLURCBDL"), 0., 100., 0.1, 0.))),
sensicb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSICB"), 0, 100, 1, 15))),
softradiuscb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SOFTRADIUSCOL"), -10.0, 1000.0, 0.5, 0.))),
blendmaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BLENDMASKCOL"), -100, 100, 1, 0))),
radmaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_RADMASKCOL"), -10.0, 1000.0, 0.1, 0.))),
chromaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_CHROMASKCOL"), -100.0, 100.0, 0.1, 0.))),
gammaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_GAMMASKCOL"), 0.25, 4.0, 0.01, 1.))),
slomaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SLOMASKCOL"), 0.0, 15.0, 0.1, 0.))),
lapmaskcb(Gtk::manage(new Adjuster(M("TP_LOCALLAB_LAPMASKCOL"), 0.0, 100.0, 0.1, 0.))),
// Denoise
noiselumf(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELUMFINE"), MINCHRO, MAXCHRO, 0.01, 0.))),
noiselumf0(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELUMFINEZERO"), MINCHRO, MAXCHRO, 0.01, 0.))),
noiselumf2(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELUMFINETWO"), MINCHRO, MAXCHRO, 0.01, 0.))),
noiselumc(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELUMCOARSE"), MINCHRO, MAXCHROCC, 0.01, 0.))),
noiselumdetail(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELUMDETAIL"), 0., 100., 0.01, 0.))),
noiselequal(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISELEQUAL"), -2, 10, 1, 7, Gtk::manage(new RTImage("circle-white-small.png")), Gtk::manage(new RTImage("circle-black-small.png"))))),
noisechrof(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISECHROFINE"), MINCHRO, MAXCHRO, 0.01, 0.))),
noisechroc(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISECHROCOARSE"), MINCHRO, MAXCHROCC, 0.01, 0.))),
noisechrodetail(Gtk::manage(new Adjuster(M("TP_LOCALLAB_NOISECHRODETAIL"), 0., 100., 0.01, 0.))),
adjblur(Gtk::manage(new Adjuster(M("TP_LOCALLAB_ADJ"), -100., 100., 1., 0., Gtk::manage(new RTImage("circle-blue-small.png")), Gtk::manage(new RTImage("circle-red-small.png"))))),
bilateral(Gtk::manage(new Adjuster(M("TP_LOCALLAB_BILATERAL"), 0, 100, 1, 0))),
sensiden(Gtk::manage(new Adjuster(M("TP_LOCALLAB_SENSIDEN"), 0, 100, 1, 20))),
detailthr(Gtk::manage(new Adjuster(M("TP_LOCALLAB_DETAILTHR"), 0, 100, 1, 0))),

// ButtonCheck widgets
// Color & Light
curvactiv(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_CURV")))),
invers(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_INVERS")))),
special(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_SPECIAL")))),
toolcol(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_TOOLCOL")))),
enaColorMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
fftColorMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_FFTCOL_MASK")))),
// Exposure
enaExpMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
enaExpMaskaft(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASKAFT")))),
inversex(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_INVERS")))),
//Shadows Highlight
enaSHMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
inverssh(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_INVERS")))),
// Vibrance
protectSkins(Gtk::manage(new Gtk::CheckButton(M("TP_VIBRANCE_PROTECTSKINS")))),
avoidColorShift(Gtk::manage(new Gtk::CheckButton(M("TP_VIBRANCE_AVOIDCOLORSHIFT")))),
pastSatTog(Gtk::manage(new Gtk::CheckButton(M("TP_VIBRANCE_PASTSATTOG")))),
enavibMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
// Blur & Noise
activlum(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ACTIV")))),
enablMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
fftwbl(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_FFTW2")))),
toolbl(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_TOOLCOL")))),
//TM
equiltm(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_EQUIL")))),
enatmMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
enatmMaskaft(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_AFTER_MASK")))),
// Retinex
equilret(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_EQUIL")))),
inversret(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_INVERS")))),
loglin(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_LOGLIN")))),
lumonly(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_LUMONLY")))),
enaretiMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),
enaretiMasktmap(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_TM_MASK")))),
fftwreti(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_FFTW")))),
// Sharpening
inverssha(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_INVERS")))),
// Local contrast
fftwlc(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_FFTW2")))),
//CBDL
enacbMask(Gtk::manage(new Gtk::CheckButton(M("TP_LOCALLAB_ENABLE_MASK")))),

// ComboBox widgets
// Color & Light
qualitycurveMethod(Gtk::manage(new MyComboBoxText())),
gridMethod(Gtk::manage(new MyComboBoxText())),
merMethod(Gtk::manage(new MyComboBoxText())),
//   toneMethod(Gtk::manage(new MyComboBoxText())),
showmaskcolMethod(Gtk::manage(new MyComboBoxText())),
showmaskcolMethodinv(Gtk::manage(new MyComboBoxText())),
mergecolMethod(Gtk::manage(new MyComboBoxText())),
csThresholdcol(Gtk::manage(new ThresholdAdjuster(M("TP_LOCALLAB_CSTHRESHOLDBLUR"), 0, 9, 0, 0, 6, 5, 0, false))),
//Exposure
showmaskexpMethod(Gtk::manage(new MyComboBoxText())),
showmaskexpMethodinv(Gtk::manage(new MyComboBoxText())),
expMethod(Gtk::manage(new MyComboBoxText())),
exnoiseMethod(Gtk::manage(new MyComboBoxText())),
//Shadows Highlight
shMethod(Gtk::manage(new MyComboBoxText())),
showmaskSHMethod(Gtk::manage(new MyComboBoxText())),
showmaskSHMethodinv(Gtk::manage(new MyComboBoxText())),
//vibrance
showmaskvibMethod(Gtk::manage(new MyComboBoxText())),
// Blur & Noise
blurMethod(Gtk::manage(new MyComboBoxText())),
//soft Method
softMethod(Gtk::manage(new MyComboBoxText())),
showmasksoftMethod(Gtk::manage(new MyComboBoxText())),
//Blur
blMethod(Gtk::manage(new MyComboBoxText())),
medMethod(Gtk::manage(new MyComboBoxText())),
showmaskblMethod(Gtk::manage(new MyComboBoxText())),
csThresholdblur(Gtk::manage(new ThresholdAdjuster(M("TP_LOCALLAB_CSTHRESHOLDBLUR"), 0, 9, 0, 0, 6, 5, 0, false))),
//TM
showmasktmMethod(Gtk::manage(new MyComboBoxText())),
// Retinex
retinexMethod(Gtk::manage(new MyComboBoxText())),
showmaskretiMethod(Gtk::manage(new MyComboBoxText())),
//Local contrast
localcontMethod(Gtk::manage(new MyComboBoxText())),
csThreshold(Gtk::manage(new ThresholdAdjuster(M("TP_LOCALLAB_CSTHRESHOLD"), 0, 9, 0, 0, 5, 5, 0, false))),
//CBDL
showmaskcbMethod(Gtk::manage(new MyComboBoxText())),

// ThresholdAdjuster widgets
// Vibrance
psThreshold(Gtk::manage(new ThresholdAdjuster(M("TP_VIBRANCE_PSTHRESHOLD"), -100., 100., 0., M("TP_VIBRANCE_PSTHRESHOLD_WEIGTHING"), 0, 0., 100., 75., M("TP_VIBRANCE_PSTHRESHOLD_SATTHRESH"), 0, this, false))),

// Other widgets
labqualcurv(Gtk::manage(new Gtk::Label(M("TP_LOCALLAB_QUALCURV_METHOD") + ":"))),
lumacontrastMinusButton(Gtk::manage(new Gtk::Button(M("TP_DIRPYREQUALIZER_LUMACONTRAST_MINUS")))),
lumaneutralButton(Gtk::manage(new Gtk::Button(M("TP_DIRPYREQUALIZER_LUMANEUTRAL")))),
lumacontrastPlusButton(Gtk::manage(new Gtk::Button(M("TP_DIRPYREQUALIZER_LUMACONTRAST_PLUS")))),
gridFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_LABGRID")))),
struFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_LABSTRUM")))),
blurFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_LABBLURM")))),
gridmerFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_LABGRIDMERG")))),
toolcolFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_TOOLMASK")))),
toolblFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_TOOLMASK")))),
mergecolFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_MERGECOLFRA")))),
merge1colFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_MERGE1COLFRA")))),
pdeFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_PDEFRA")))),
fatFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_FATFRA")))),
gradcolFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRADFRA")))),
gradFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRADFRA")))),
gradFramemask(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRADFRA")))),
gradSHFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRADFRA")))),
gradvibFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRADFRA")))),
fatSHFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_FATSHFRA")))),
gamFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GAMFRA")))),
dehaFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_DEHAFRA")))),
retiFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_RETIFRA")))),
retitoolFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_RETITOOLFRA")))),
residFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_RESID")))),
clariFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_CLARIFRA")))),
grainFrame(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_GRAINFRA")))),
//    retiBox(Gtk::manage(new Gtk::Frame(M("TP_LOCALLAB_CLARIFRA")))),
retiBox(Gtk::manage(new ToolParamBlock())),
maskretiBox(Gtk::manage(new ToolParamBlock())),
mask7(Gtk::manage(new ToolParamBlock())),
labgrid(Gtk::manage(new LabGrid(EvLocallabLabGridValue, M("TP_LOCALLAB_LABGRID_VALUES")))),
labgridmerg(Gtk::manage(new LabGrid(EvLocallabLabGridmergValue, M("TP_LOCALLAB_LABGRID_VALUES"), false))),

mMLabels(Gtk::manage(new Gtk::Label("---"))),
transLabels(Gtk::manage(new Gtk::Label("---"))),
transLabels2(Gtk::manage(new Gtk::Label("---"))),

// Others
ctboxsoftmethod(Gtk::manage(new Gtk::HBox())),
ctboxexpmethod(Gtk::manage(new Gtk::HBox())),
nextmin(0.),
nextmax(0.),
nextminiT(0.),
nextmaxiT(0.),
nextmeanT(0.),
nextminT(0.),
nextmaxT(0.),
nextsigma(0.),
defparams(nullptr),
defpedited(nullptr),
pe(nullptr)
{
    ToolVBox* const panel = Gtk::manage(new ToolVBox());
    const bool showtooltip = options.showtooltip;
    complexsoft = options.complexity;

    CurveListener::setMulti(true);
    rtengine::procparams::LocallabParams::LocallabSpot defSpot;

    // Settings
    expsettings->getExpander()->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expsettings->getExpander()));
    expsettings->setLevel(2);

    panel->pack_start(*expsettings->getExpander(), false, false);

    // Color & Light
    expcolor->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expcolor));
    enablecolorConn = expcolor->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expcolor));
    // expcolor->set_tooltip_text(M("TP_LOCALLAB_EXPCOLOR_TOOLTIP"));
    setExpandAlignProperties(expmaskcol, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskcol->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskcol));
    expmaskcol->setLevel(2);
    setExpandAlignProperties(expmaskcol1, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskcol1->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskcol1));
    expmaskcol1->setLevel(2);

    if (showtooltip) {
        expmaskcol->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    curvactivConn = curvactiv->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::curvactivChanged));
    lightness->setAdjusterListener(this);

    if (showtooltip) {
        lightness->set_tooltip_text(M("TP_LOCALLAB_EXPCOLOR_TOOLTIP"));
    }

    contrast->setAdjusterListener(this);

    chroma->setAdjusterListener(this);

    if (showtooltip) {
        sensi->set_tooltip_text(M("TP_LOCALLAB_SENSI_TOOLTIP"));
    }

    sensi->setAdjusterListener(this);

    strengthgrid->setAdjusterListener(this);
    structcol->setAdjusterListener(this);
    blurcolde->setAdjusterListener(this);


//    blurcol->setAdjusterListener(this);
    contcol->setAdjusterListener(this);
    strcol->setAdjusterListener(this);
    angcol->setAdjusterListener(this);
    strcolab->setAdjusterListener(this);
    strcolh->setAdjusterListener(this);

    blendmaskcol->setAdjusterListener(this);
    radmaskcol->setLogScale(10, -10);
    radmaskcol->setAdjusterListener(this);
    chromaskcol->setAdjusterListener(this);
    gammaskcol->setAdjusterListener(this);
    slomaskcol->setAdjusterListener(this);
    shadmaskcol->setAdjusterListener(this);
    strumaskcol->setAdjusterListener(this);

    softradiuscol->setLogScale(10, -10);
    softradiuscol->setAdjusterListener(this);
    opacol->setAdjusterListener(this);
    conthrcol->setAdjusterListener(this);
    lapmaskcol->setAdjusterListener(this);
    mercol->setAdjusterListener(this);
    merlucol->setAdjusterListener(this);

    if (showtooltip) {
        radmaskcol->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskcol->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //   chromaskcol->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }

    if (showtooltip) {
        strcol->set_tooltip_text(M("TP_LOCALLAB_GRADGEN_TOOLTIP"));
        strcolab->set_tooltip_text(M("TP_LOCALLAB_GRADSTRAB_TOOLTIP"));
        strcolh->set_tooltip_text(M("TP_LOCALLAB_GRADSTRHUE_TOOLTIP"));
        angcol->set_tooltip_text(M("TP_LOCALLAB_GRADANG_TOOLTIP"));
    }

    qualitycurveMethod->append(M("TP_LOCALLAB_CURVNONE"));
    qualitycurveMethod->append(M("TP_LOCALLAB_CURVCURR"));
    qualitycurveMethod->set_active(0);

    if (showtooltip) {
        qualitycurveMethod->set_tooltip_markup(M("TP_LOCALLAB_CURVEMETHOD_TOOLTIP"));
    }

    qualitycurveMethodConn = qualitycurveMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::qualitycurveMethodChanged));

    gridMethod->append(M("TP_LOCALLAB_GRIDONE"));
    gridMethod->append(M("TP_LOCALLAB_GRIDTWO"));
    gridMethod->set_active(0);
    gridMethodConn = gridMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::gridMethodChanged));


    merMethod->append(M("TP_LOCALLAB_MRONE"));
    merMethod->append(M("TP_LOCALLAB_MRTWO"));
    merMethod->append(M("TP_LOCALLAB_MRTHR"));
    merMethod->append(M("TP_LOCALLAB_MRFOU"));
    merMethod->append(M("TP_LOCALLAB_MRFIV"));
    merMethod->set_active(0);
    merMethodConn = merMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::merMethodChanged));

    toneMethod->append(M("TP_EXPOSURE_TCMODE_STANDARD"));
    toneMethod->append(M("TP_EXPOSURE_TCMODE_WEIGHTEDSTD"));
    toneMethod->append(M("TP_EXPOSURE_TCMODE_LUMINANCE"));
    toneMethod->append(M("TP_EXPOSURE_TCMODE_FILMLIKE"));

    toneMethod->set_active(0);
    toneMethodConn = toneMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::toneMethodChanged));

    llCurveEditorG->setCurveListener(this);
    llshape->setResetCurve(DiagonalCurveType(defSpot.llcurve.at(0)), defSpot.llcurve);

    if (showtooltip) {
        llshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone> zero_one_shape = {
        {0., 0., 0., 0.},
        {1., 1., 1., 1.}
    };

    const std::vector<GradientMilestone>& mllshape = zero_one_shape;
    llshape->setBottomBarBgGradient(mllshape);
    llshape->setLeftBarBgGradient(mllshape);

    ccshape->setResetCurve(DiagonalCurveType(defSpot.cccurve.at(0)), defSpot.cccurve);

    if (showtooltip) {
        ccshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    ccshape->setBottomBarColorProvider(this, 2);
    ccshape->setLeftBarColorProvider(this, 7);

    if (showtooltip) {
        clshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mclshape = zero_one_shape;

    llCurveEditorG->curveListComplete();

    clCurveEditorG->setCurveListener(this);
    clshape->setResetCurve(DiagonalCurveType(defSpot.clcurve.at(0)), defSpot.clcurve);

    clshape->setBottomBarBgGradient(mclshape);
    clshape->setLeftBarColorProvider(this, 7);

    if (showtooltip) {
        lcshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mlcshape = zero_one_shape;
    lcshape->setResetCurve(DiagonalCurveType(defSpot.lccurve.at(0)), defSpot.lccurve);

    lcshape->setBottomBarColorProvider(this, 2);
    lcshape->setLeftBarBgGradient(mlcshape);

    clCurveEditorG->curveListComplete();

    // llCurveEditorG->newLine();
    HCurveEditorG->setCurveListener(this);

    LHshape->setIdentityValue(0.);
    LHshape->setResetCurve(FlatCurveType(defSpot.LHcurve.at(0)), defSpot.LHcurve);

    if (showtooltip) {
        LHshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    LHshape->setCurveColorProvider(this, 1);

    const std::vector<GradientMilestone> six_shape =
    []() -> std::vector<GradientMilestone> {
        std::vector<GradientMilestone> res;

        float R, G, B;

        for (int i = 0; i < 7; ++i)
        {
            const float x = static_cast<float>(i) * (1.f / 6.f);
            Color::hsv2rgb01(x, 0.5f, 0.5f, R, G, B);
            res.emplace_back(x, R, G, B);
        }

        return res;
    }();

    const std::vector<GradientMilestone>& mLHshape = six_shape;
    LHshape->setBottomBarBgGradient(mLHshape);
    HCurveEditorG->curveListComplete();
    H2CurveEditorG->setCurveListener(this);

    HHshape->setIdentityValue(0.);
    HHshape->setResetCurve(FlatCurveType(defSpot.HHcurve.at(0)), defSpot.HHcurve);

    if (showtooltip) {
        HHshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    HHshape->setCurveColorProvider(this, 1);

    const std::vector<GradientMilestone>& mHHshape = six_shape;
    HHshape->setBottomBarBgGradient(mHHshape);

    H2CurveEditorG->curveListComplete();


    rgbCurveEditorG->setCurveListener(this);
    rgbshape->setResetCurve(DiagonalCurveType(defSpot.rgbcurve.at(0)), defSpot.rgbcurve);

    if (showtooltip) {
        rgbshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mrgbshape = zero_one_shape;
    rgbshape->setBottomBarBgGradient(mrgbshape);
    rgbshape->setLeftBarBgGradient(mrgbshape);
    rgbCurveEditorG->curveListComplete();

    inversConn  = invers->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::inversChanged));
    specialConn  = special->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::specialChanged));
    toolcolConn  = toolcol->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::toolcolChanged));


    mergecolMethod->append(M("TP_LOCALLAB_MERONE"));
    mergecolMethod->append(M("TP_LOCALLAB_MERTWO"));
    mergecolMethod->append(M("TP_LOCALLAB_MERTHR"));
    mergecolMethod->append(M("TP_LOCALLAB_MERFOU"));
    mergecolMethod->append(M("TP_LOCALLAB_MERFIV"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSIX"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSEV"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSEV0"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSEV1"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSEV2"));
    mergecolMethod->append(M("TP_LOCALLAB_MERHEI"));
    mergecolMethod->append(M("TP_LOCALLAB_MERNIN"));
    mergecolMethod->append(M("TP_LOCALLAB_MERTEN"));
    mergecolMethod->append(M("TP_LOCALLAB_MERELE"));
    mergecolMethod->append(M("TP_LOCALLAB_MERTWE"));
    mergecolMethod->append(M("TP_LOCALLAB_MERTHI"));
    mergecolMethod->append(M("TP_LOCALLAB_MERFOR"));
    mergecolMethod->append(M("TP_LOCALLAB_MERHUE"));
    mergecolMethod->append(M("TP_LOCALLAB_MERSAT"));
    mergecolMethod->append(M("TP_LOCALLAB_MERCOL"));
    mergecolMethod->append(M("TP_LOCALLAB_MERLUM"));
    mergecolMethod->set_active(0);
    mergecolMethodConn = mergecolMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::mergecolMethodChanged));


    showmaskcolMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskcolMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskcolMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskcolMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskcolMethod->append(M("TP_LOCALLAB_SHOWSTRUC"));
    showmaskcolMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));

    showmaskcolMethod->set_active(0);

    if (showtooltip) {
        showmaskcolMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskcolMethodConn  = showmaskcolMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskcolMethodChanged));

    showmaskcolMethodinv->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskcolMethodinv->append(M("TP_LOCALLAB_SHOWMASK"));

    showmaskcolMethodinv->set_active(0);

    if (showtooltip) {
        showmaskcolMethodinv->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskcolMethodConninv  = showmaskcolMethodinv->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskcolMethodChangedinv));


    enaColorMaskConn = enaColorMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaColorMaskChanged));
    fftColorMaskConn = fftColorMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::fftColorMaskChanged));

    maskCurveEditorG->setCurveListener(this);

    CCmaskshape->setIdentityValue(0.);
    CCmaskshape->setResetCurve(FlatCurveType(defSpot.CCmaskcurve.at(0)), defSpot.CCmaskcurve);

    if (showtooltip) {
        CCmaskshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskshape->setBottomBarColorProvider(this, 7);

    LLmaskshape->setIdentityValue(0.);
    LLmaskshape->setResetCurve(FlatCurveType(defSpot.LLmaskcurve.at(0)), defSpot.LLmaskcurve);

    if (showtooltip) {
        LLmaskshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskshape->setBottomBarBgGradient(mllshape);

    if (showtooltip) {
        LLmaskshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

//   maskCurveEditorG->curveListComplete();
//    maskHCurveEditorG->setCurveListener(this);

    HHmaskshape->setIdentityValue(0.);
    HHmaskshape->setResetCurve(FlatCurveType(defSpot.HHmaskcurve.at(0)), defSpot.HHmaskcurve);

    if (showtooltip) {
        HHmaskshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

//    maskHCurveEditorG->setCurveListener(this);

    HHmaskshape->setCurveColorProvider(this, 6);
    HHmaskshape->setBottomBarColorProvider(this, 6);
    maskCurveEditorG->curveListComplete();

    maskHCurveEditorG->setCurveListener(this);

    HHhmaskshape->setIdentityValue(0.);
    HHhmaskshape->setResetCurve(FlatCurveType(defSpot.HHhmaskcurve.at(0)), defSpot.HHhmaskcurve);

    if (showtooltip) {
        HHhmaskshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_LL_TOOLTIP"));
    }

    HHhmaskshape->setCurveColorProvider(this, 6);
    HHhmaskshape->setBottomBarColorProvider(this, 6);

    maskHCurveEditorG->curveListComplete();

    mask2CurveEditorG->setCurveListener(this);
    Lmaskshape->setResetCurve(DiagonalCurveType(defSpot.Lmaskcurve.at(0)), defSpot.Lmaskcurve);

    if (showtooltip) {
        Lmaskshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskshape = zero_one_shape;
    Lmaskshape->setBottomBarBgGradient(mLmaskshape);
    Lmaskshape->setLeftBarBgGradient(mLmaskshape);
    mask2CurveEditorG->curveListComplete();

    mask2CurveEditorGwav->setCurveListener(this);
    LLmaskcolshapewav->setIdentityValue(0.);
    LLmaskcolshapewav->setResetCurve(FlatCurveType(defSpot.LLmaskcolcurvewav.at(0)), defSpot.LLmaskcolcurvewav);

    if (showtooltip) {
        LLmaskcolshapewav->setTooltip(M("TP_LOCALLAB_LMASK_LEVEL_TOOLTIP"));
    }

    LLmaskcolshapewav->setBottomBarBgGradient(mllshape);


    mask2CurveEditorGwav->curveListComplete();
    csThresholdcol->setAdjusterListener(this);

    if (showtooltip) {
        opacol->set_tooltip_text(M("TP_LOCALLAB_MERGEOPA_TOOLTIP"));
        conthrcol->set_tooltip_text(M("TP_LOCALLAB_MERGEOPA_TOOLTIP"));
    }

    gradcolFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const gradcolBox = Gtk::manage(new ToolParamBlock());
    gradcolBox->pack_start(*strcol);

    if (complexsoft < 2) {
        gradcolBox->pack_start(*strcolab);
    }

    if (complexsoft < 2) {
        gradcolBox->pack_start(*strcolh);
    }

    gradcolBox->pack_start(*angcol);
    gradcolFrame->add(*gradcolBox);


    ToolParamBlock* const colorBox = Gtk::manage(new ToolParamBlock());
    Gtk::Frame* const superFrame = Gtk::manage(new Gtk::Frame());
    superFrame->set_label_align(0.025, 0.5);
    superFrame->set_label_widget(*curvactiv);
    ToolParamBlock* const superBox = Gtk::manage(new ToolParamBlock());
    superBox->pack_start(*lightness);
    superBox->pack_start(*contrast);
    superBox->pack_start(*chroma);
    gridFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const gridBox = Gtk::manage(new ToolParamBlock());
    gridBox->pack_start(*labgrid);
    gridBox->pack_start(*gridMethod);
    gridBox->pack_start(*strengthgrid);
    gridFrame->add(*gridBox);

    if (complexsoft < 2) {
        superBox->pack_start(*gridFrame);
    }

    superFrame->add(*superBox);
    colorBox->pack_start(*superFrame);
    colorBox->pack_start(*sensi);

    if (complexsoft < 2) {
        colorBox->pack_start(*structcol);
    }

    if (complexsoft < 2) {
        colorBox->pack_start(*blurcolde);
    }


    colorBox->pack_start(*gradcolFrame);

    if (complexsoft < 2) {
        colorBox->pack_start(*softradiuscol);
    }

    Gtk::HBox* const qualcurvbox = Gtk::manage(new Gtk::HBox());
    qualcurvbox->pack_start(*labqualcurv, Gtk::PACK_SHRINK, 4);
    qualcurvbox->pack_start(*qualitycurveMethod);
    colorBox->pack_start(*qualcurvbox);
    colorBox->pack_start(*llCurveEditorG, Gtk::PACK_SHRINK, 4);

    if (complexsoft < 2) {
        colorBox->pack_start(*clCurveEditorG, Gtk::PACK_SHRINK, 4);
    }

    if (complexsoft < 2) {
        colorBox->pack_start(*HCurveEditorG, Gtk::PACK_SHRINK, 4);
    }

    if (complexsoft < 2) {
        colorBox->pack_start(*H2CurveEditorG, Gtk::PACK_SHRINK, 4);
    }

    if (complexsoft < 2) {
        colorBox->pack_start(*rgbCurveEditorG, Gtk::PACK_SHRINK, 4);
    }

    if (complexsoft < 2) {
        colorBox->pack_start(*special);
    }

    colorBox->pack_start(*invers);
    Gtk::HSeparator* const separatormer = Gtk::manage(new  Gtk::HSeparator());

    if (showtooltip) {
        gridmerFrame->set_tooltip_text(M("TP_LOCALLAB_GRIDFRAME_TOOLTIP"));
    }

    ToolParamBlock* const gridmerBox = Gtk::manage(new ToolParamBlock());
    gridmerFrame->set_label_align(0.025, 0.5);
    gridmerBox->pack_start(*labgridmerg);
    gridmerBox->pack_start(*merlucol);
    gridmerFrame->add(*gridmerBox);

    mergecolFrame->set_label_align(0.025, 0.5);
    merge1colFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const mergecolBox = Gtk::manage(new ToolParamBlock());
    mergecolBox->pack_start(*separatormer, Gtk::PACK_SHRINK, 2);
    mergecolBox->pack_start(*mergecolMethod);
    mergecolBox->pack_start(*mercol);
    mergecolBox->pack_start(*opacol);
    mergecolBox->pack_start(*conthrcol);
    mergecolBox->pack_start(*gridmerFrame);

    if (complexsoft < 2) {
        merge1colFrame->add(*mergecolBox);
    }


    ToolParamBlock* const maskcolBox = Gtk::manage(new ToolParamBlock());
    maskcolBox->pack_start(*showmaskcolMethod, Gtk::PACK_SHRINK, 4);
    maskcolBox->pack_start(*showmaskcolMethodinv, Gtk::PACK_SHRINK, 4);
    maskcolBox->pack_start(*enaColorMask, Gtk::PACK_SHRINK, 0);
    maskcolBox->pack_start(*maskCurveEditorG, Gtk::PACK_SHRINK, 4);

    struFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const strumBox = Gtk::manage(new ToolParamBlock());
    if (complexsoft < 2) {
        strumBox->pack_start(*strumaskcol);
        strumBox->pack_start(*toolcol);
    }
    struFrame->add(*strumBox);

    if (complexsoft < 2) {
        maskcolBox->pack_start(*struFrame, Gtk::PACK_SHRINK, 0);
    }

    blurFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const blurmBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 1) {
        blurmBox->pack_start(*fftColorMask, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        if(complexsoft < 1) {
           blurcol->setLimits (0., 500., 0.5, 0.2);
        }
        blurmBox->pack_start(*contcol);
        blurcol->setAdjusterListener(this);
        blurmBox->pack_start(*blurcol);
    }
    blurFrame->add(*blurmBox);

    if (complexsoft < 2) {
        maskcolBox->pack_start(*blurFrame, Gtk::PACK_SHRINK, 0);
    }

    toolcolFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const toolcolBox = Gtk::manage(new ToolParamBlock());

    maskcolBox->pack_start(*blendmaskcol, Gtk::PACK_SHRINK, 0);
    toolcolBox->pack_start(*radmaskcol, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        toolcolBox->pack_start(*lapmaskcol, Gtk::PACK_SHRINK, 0);
    }

    toolcolBox->pack_start(*chromaskcol, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        toolcolBox->pack_start(*gammaskcol, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        toolcolBox->pack_start(*slomaskcol, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        toolcolBox->pack_start(*shadmaskcol, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        toolcolBox->pack_start(*maskHCurveEditorG, Gtk::PACK_SHRINK, 4);
    }

    toolcolBox->pack_start(*mask2CurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor

    if (complexsoft < 1) {
        toolcolBox->pack_start(*mask2CurveEditorGwav, Gtk::PACK_SHRINK, 4);    // Padding is mandatory to correct behavior of curve editor
    }

    if (complexsoft < 1) {
        toolcolBox->pack_start(*csThresholdcol, Gtk::PACK_SHRINK, 0);
    }

    toolcolFrame->add(*toolcolBox);
    maskcolBox->pack_start(*toolcolFrame);

    mergecolFrame->add(*maskcolBox);
    ToolParamBlock* const mask7Box = Gtk::manage(new ToolParamBlock());
    mask7Box->pack_start(*merMethod);
    mask7->pack_start(*merge1colFrame);
    mask7Box->pack_start(*mask7);
    expmaskcol1->add(*mask7Box, false);
    expmaskcol->add(*mergecolFrame, false);

    if (complexsoft < 2) {
        colorBox->pack_start(*expmaskcol1);
    }

    colorBox->pack_start(*expmaskcol);

    expcolor->add(*colorBox, false);
    expcolor->setLevel(2);

    panel->pack_start(*expcolor, false, false);

    // Exposure
    expexpose->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expexpose));
    enableexposeConn = expexpose->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expexpose));

    if (showtooltip) {
        expexpose->set_tooltip_text(M("TP_LOCALLAB_EXPOSURE_TOOLTIP"));
    }

    expMethod->append(M("TP_LOCALLAB_STD"));
    if (complexsoft == 1) {
        expMethod->append(M("TP_LOCALLAB_PDE"));
    }
    if (complexsoft == 0) {
        expMethod->append(M("TP_LOCALLAB_PDE2"));
    }
    
    expMethod->set_active(0);
    expMethodConn  = expMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::expMethodChanged));

    if (showtooltip) {
        expMethod->set_tooltip_text(M("TP_LOCALLAB_EXPMETHOD_TOOLTIP"));
    }

    Gtk::Label* const labelexpmethod = Gtk::manage(new Gtk::Label(M("TP_LOCALLAB_NOISEMETH") + ":"));
    ctboxexpmethod->pack_start(*labelexpmethod, Gtk::PACK_SHRINK, 4);

    exnoiseMethod->append(M("TP_LOCALLAB_NONENOISE"));
    exnoiseMethod->append(M("TP_LOCALLAB_MEDIAN"));
    exnoiseMethod->append(M("TP_LOCALLAB_WEDIANHI"));
    exnoiseMethod->set_active(0);
    exnoiseMethodConn  = exnoiseMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::exnoiseMethodChanged));

    if (showtooltip) {
        exnoiseMethod->set_tooltip_text(M("TP_LOCALLAB_EXPMETHOD_TOOLTIP"));
    }

    ctboxexpmethod->pack_start(*exnoiseMethod);

    setExpandAlignProperties(expmaskexp, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskexp->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskexp));
    expmaskexp->setLevel(2);

    if (showtooltip) {
        expmaskexp->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    expcomp->setAdjusterListener(this);

    hlcompr->setAdjusterListener(this);

    hlcomprthresh->setAdjusterListener(this);

    black->setAdjusterListener(this);
    shadex->setAdjusterListener(this);

    shcompr->setAdjusterListener(this);
    expchroma->setAdjusterListener(this);

    if (showtooltip) {
        warm->set_tooltip_text(M("TP_LOCALLAB_WARM_TOOLTIP"));
    }

    warm->setAdjusterListener(this);

    if (showtooltip) {
        sensiex->set_tooltip_text(M("TP_LOCALLAB_SENSI_TOOLTIP"));
    }

    sensiex->setAdjusterListener(this);
    inversexConn  = inversex->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::inversexChanged));

    structexp->setAdjusterListener(this);

    blurexpde->setAdjusterListener(this);
    strexp->setAdjusterListener(this);
    angexp->setAdjusterListener(this);

    blendmaskexp->setAdjusterListener(this);
    radmaskexp->setLogScale(10, -10);
    radmaskexp->setAdjusterListener(this);
    chromaskexp->setAdjusterListener(this);
    gammaskexp->setAdjusterListener(this);
    slomaskexp->setAdjusterListener(this);
    lapmaskexp->setAdjusterListener(this);
    strmaskexp->setAdjusterListener(this);
    angmaskexp->setAdjusterListener(this);
    softradiusexp->setLogScale(10, -10);
    softradiusexp->setAdjusterListener(this);
    laplacexp->setAdjusterListener(this);
    balanexp->setAdjusterListener(this);
    linear->setAdjusterListener(this);
    gamm->setAdjusterListener(this);
    fatamount->setAdjusterListener(this);
    fatdetail->setAdjusterListener(this);
    fatanchor->setAdjusterListener(this);
    fatlevel->setAdjusterListener(this);

    if (showtooltip) {
        strexp->set_tooltip_text(M("TP_LOCALLAB_GRADGEN_TOOLTIP"));
        strmaskexp->set_tooltip_text(M("TP_LOCALLAB_GRADGEN_TOOLTIP"));
        angexp->set_tooltip_text(M("TP_LOCALLAB_GRADANG_TOOLTIP"));
        angmaskexp->set_tooltip_text(M("TP_LOCALLAB_GRADANG_TOOLTIP"));
    }

    if (showtooltip) {
        radmaskexp->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskexp->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //      chromaskexp->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }

    curveEditorG->setCurveListener(this);

    shapeexpos->setResetCurve(DiagonalCurveType(defSpot.excurve.at(0)), defSpot.excurve);

    if (showtooltip) {
        shapeexpos->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_TONES_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mshapeexpos = zero_one_shape;
    shapeexpos->setBottomBarBgGradient(mshapeexpos);
    shapeexpos->setLeftBarBgGradient(mshapeexpos);

    curveEditorG->curveListComplete();

    enaExpMaskConn = enaExpMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaExpMaskChanged));
    enaExpMaskaftConn = enaExpMaskaft->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaExpMaskaftChanged));

    showmaskexpMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskexpMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskexpMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskexpMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskexpMethod->append(M("TP_LOCALLAB_SHOWSTRUCEX"));
    showmaskexpMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));

    showmaskexpMethod->set_active(0);

    if (showtooltip) {
        showmaskexpMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskexpMethodConn  = showmaskexpMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskexpMethodChanged));

    showmaskexpMethodinv->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskexpMethodinv->append(M("TP_LOCALLAB_SHOWMASK"));

    showmaskexpMethodinv->set_active(0);

    if (showtooltip) {
        showmaskexpMethodinv->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskexpMethodConninv  = showmaskexpMethodinv->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskexpMethodChangedinv));

    maskexpCurveEditorG->setCurveListener(this);

    CCmaskexpshape->setIdentityValue(0.);
    CCmaskexpshape->setResetCurve(FlatCurveType(defSpot.CCmaskexpcurve.at(0)), defSpot.CCmaskexpcurve);

    if (showtooltip) {
        CCmaskexpshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskexpshape->setBottomBarColorProvider(this, 7);

    LLmaskexpshape->setIdentityValue(0.);
    LLmaskexpshape->setResetCurve(FlatCurveType(defSpot.LLmaskexpcurve.at(0)), defSpot.LLmaskexpcurve);

    if (showtooltip) {
        LLmaskexpshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskexpshape->setBottomBarBgGradient(mllshape);

    HHmaskexpshape->setIdentityValue(0.);
    HHmaskexpshape->setResetCurve(FlatCurveType(defSpot.HHmaskexpcurve.at(0)), defSpot.HHmaskexpcurve);

    if (showtooltip) {
        HHmaskexpshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskexpshape->setCurveColorProvider(this, 6);
    HHmaskexpshape->setBottomBarColorProvider(this, 6);

    maskexpCurveEditorG->curveListComplete();

    mask2expCurveEditorG->setCurveListener(this);
    Lmaskexpshape->setResetCurve(DiagonalCurveType(defSpot.Lmaskexpcurve.at(0)), defSpot.Lmaskexpcurve);

    if (showtooltip) {
        Lmaskexpshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskexpshape = zero_one_shape;
    Lmaskexpshape->setBottomBarBgGradient(mLmaskexpshape);
    Lmaskexpshape->setLeftBarBgGradient(mLmaskexpshape);
    mask2expCurveEditorG->curveListComplete();


    pdeFrame->set_label_align(0.025, 0.5);

    if (showtooltip) {
        pdeFrame->set_tooltip_text(M("TP_LOCALLAB_PDEFRAME_TOOLTIP"));
    }


    ToolParamBlock* const pdeBox = Gtk::manage(new ToolParamBlock());
    pdeBox->pack_start(*laplacexp);
    pdeBox->pack_start(*linear);
    pdeBox->pack_start(*balanexp);
    pdeBox->pack_start(*gamm);
    pdeBox->pack_start(*ctboxexpmethod);

    fatFrame->set_label_align(0.025, 0.5);

    if (showtooltip) {
        fatFrame->set_tooltip_text(M("TP_LOCALLAB_FATFRAME_TOOLTIP"));
    }

    ToolParamBlock* const fatBox = Gtk::manage(new ToolParamBlock());
    fatBox->pack_start(*fatamount);
    fatBox->pack_start(*fatdetail);

    if (complexsoft < 2) {
        fatBox->pack_start(*fatlevel);
    }

    fatBox->pack_start(*fatanchor);
    pdeFrame->add(*pdeBox);
    fatFrame->add(*fatBox);

    gradFrame->set_label_align(0.025, 0.5);

    ToolParamBlock* const gradBox = Gtk::manage(new ToolParamBlock());
    gradBox->pack_start(*strexp);
    gradBox->pack_start(*angexp);
    gradFrame->add(*gradBox);

    gradFramemask->set_label_align(0.025, 0.5);

    ToolParamBlock* const gradmaskBox = Gtk::manage(new ToolParamBlock());
    gradmaskBox->pack_start(*strmaskexp);
    gradmaskBox->pack_start(*angmaskexp);
    gradFramemask->add(*gradmaskBox);


    ToolParamBlock* const exposeBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        exposeBox->pack_start(*expMethod);
    }

    if (complexsoft < 1) {
        exposeBox->pack_start(*pdeFrame);
    }

    if (complexsoft < 2) {
        exposeBox->pack_start(*black);
    }

//    if (complexsoft < 2) {
    exposeBox->pack_start(*fatFrame);
//    }

    exposeBox->pack_start(*expcomp);

    if (complexsoft < 2) {
        exposeBox->pack_start(*hlcompr);
        exposeBox->pack_start(*hlcomprthresh);
        exposeBox->pack_start(*shadex);
        exposeBox->pack_start(*shcompr);
        exposeBox->pack_start(*expchroma);
    }

    exposeBox->pack_start(*warm);
    exposeBox->pack_start(*sensiex);

    if (complexsoft < 2) {
        exposeBox->pack_start(*structexp);
    }

    if (complexsoft < 2) {
        exposeBox->pack_start(*blurexpde);
    }

    exposeBox->pack_start(*gradFrame);

    if (complexsoft < 2) {
        exposeBox->pack_start(*softradiusexp);
    }

    exposeBox->pack_start(*curveEditorG, Gtk::PACK_SHRINK, 4);

    if (complexsoft < 2) {
        exposeBox->pack_start(*inversex);
    }

    
    ToolParamBlock* const maskexpBox = Gtk::manage(new ToolParamBlock());
    maskexpBox->pack_start(*showmaskexpMethod, Gtk::PACK_SHRINK, 4);
    maskexpBox->pack_start(*showmaskexpMethodinv, Gtk::PACK_SHRINK, 4);
    maskexpBox->pack_start(*enaExpMask, Gtk::PACK_SHRINK, 0);
//    maskexpBox->pack_start(*enaExpMaskaft, Gtk::PACK_SHRINK, 0);
    maskexpBox->pack_start(*maskexpCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    maskexpBox->pack_start(*blendmaskexp, Gtk::PACK_SHRINK, 0);
    maskexpBox->pack_start(*radmaskexp, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        maskexpBox->pack_start(*lapmaskexp, Gtk::PACK_SHRINK, 0);
    }

    maskexpBox->pack_start(*chromaskexp, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        maskexpBox->pack_start(*gammaskexp, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        maskexpBox->pack_start(*slomaskexp, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        maskexpBox->pack_start(*gradFramemask);
    }

    maskexpBox->pack_start(*mask2expCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    expmaskexp->add(*maskexpBox, false);
    exposeBox->pack_start(*expmaskexp);

    expexpose->add(*exposeBox, false);
    expexpose->setLevel(2);

    panel->pack_start(*expexpose, false, false);



//shadow highlight
    expshadhigh->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expshadhigh));
    enableshadhighConn = expshadhigh->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expshadhigh));

    if (showtooltip) {
        expshadhigh->set_tooltip_text(M("TP_LOCALLAB_SHADOWHIGHLIGHT_TOOLTIP"));
    }

    shMethod->append(M("TP_LOCALLAB_SH1"));
    shMethod->append(M("TP_LOCALLAB_SH2"));
    shMethod->set_active(0);
    shMethodConn  = shMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::shMethodChanged));

    if (showtooltip) {
        expMethod->set_tooltip_text(M("TP_LOCALLAB_EXPMETHOD_TOOLTIP"));
    }

    /*
        ctboxshmethod = Gtk::manage(new Gtk::HBox());
        Gtk::Label* const labelshmethod = Gtk::manage(new Gtk::Label(M("TP_LOCALLAB_NOISEMETH") + ":"));
        ctboxshmethod->pack_start(*labelshmethod, Gtk::PACK_SHRINK, 4);
    */
    setExpandAlignProperties(expmasksh, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmasksh->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmasksh));
    expmasksh->setLevel(2);

    if (showtooltip) {
        expmasksh->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    highlights->setAdjusterListener(this);
    h_tonalwidth->setAdjusterListener(this);
    shadows->setAdjusterListener(this);
    s_tonalwidth->setAdjusterListener(this);
    sh_radius->setAdjusterListener(this);
    sensihs->setAdjusterListener(this);
    blendmaskSH->setAdjusterListener(this);
    radmaskSH->setLogScale(10, -10);
    radmaskSH->setAdjusterListener(this);
    blurSHde->setAdjusterListener(this);
    strSH->setAdjusterListener(this);
    angSH->setAdjusterListener(this);
    chromaskSH->setAdjusterListener(this);
    gammaskSH->setAdjusterListener(this);
    slomaskSH->setAdjusterListener(this);
    lapmaskSH->setAdjusterListener(this);
    detailSH->setAdjusterListener(this);
    fatamountSH->setAdjusterListener(this);
    fatanchorSH->setAdjusterListener(this);
    gamSH->setAdjusterListener(this);
    sloSH->setAdjusterListener(this);

    if (showtooltip) {
        radmaskSH->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskSH->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
    }

    if (showtooltip) {
        strSH->set_tooltip_text(M("TP_LOCALLAB_GRADGEN_TOOLTIP"));
        angSH->set_tooltip_text(M("TP_LOCALLAB_GRADANG_TOOLTIP"));
    }

    enaSHMaskConn = enaSHMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaSHMaskChanged));
    inversshConn  = inverssh->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::inversshChanged));

    showmaskSHMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskSHMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskSHMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskSHMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskSHMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));

//    showmaskSHMethod->append(M("TP_LOCALLAB_SHOWSTRUC"));

    showmaskSHMethod->set_active(0);

    if (showtooltip) {
        showmaskSHMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskSHMethodConn  = showmaskSHMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskSHMethodChanged));

    showmaskSHMethodinv->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskSHMethodinv->append(M("TP_LOCALLAB_SHOWMASK"));

    showmaskSHMethodinv->set_active(0);

    if (showtooltip) {
        showmaskSHMethodinv->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskSHMethodConninv  = showmaskSHMethodinv->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskSHMethodChangedinv));

    maskSHCurveEditorG->setCurveListener(this);

    CCmaskSHshape->setIdentityValue(0.);
    CCmaskSHshape->setResetCurve(FlatCurveType(defSpot.CCmaskSHcurve.at(0)), defSpot.CCmaskSHcurve);

    if (showtooltip) {
        CCmaskSHshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskSHshape->setBottomBarColorProvider(this, 7);

    LLmaskSHshape->setIdentityValue(0.);
    LLmaskSHshape->setResetCurve(FlatCurveType(defSpot.LLmaskSHcurve.at(0)), defSpot.LLmaskSHcurve);

    if (showtooltip) {
        LLmaskSHshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskSHshape->setBottomBarBgGradient(mllshape);

    HHmaskSHshape->setIdentityValue(0.);
    HHmaskSHshape->setResetCurve(FlatCurveType(defSpot.HHmaskSHcurve.at(0)), defSpot.HHmaskSHcurve);

    if (showtooltip) {
        HHmaskSHshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskSHshape->setCurveColorProvider(this, 6);
    HHmaskSHshape->setBottomBarColorProvider(this, 6);

    maskSHCurveEditorG->curveListComplete();

    mask2SHCurveEditorG->setCurveListener(this);
    LmaskSHshape->setResetCurve(DiagonalCurveType(defSpot.LmaskSHcurve.at(0)), defSpot.LmaskSHcurve);

    if (showtooltip) {
        LmaskSHshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskSHshape = zero_one_shape;
    LmaskSHshape->setBottomBarBgGradient(mLmaskSHshape);
    LmaskSHshape->setLeftBarBgGradient(mLmaskSHshape);
    mask2SHCurveEditorG->curveListComplete();

    ToolParamBlock* const shadhighBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        shadhighBox->pack_start(*shMethod);
    }

    for (int i = 0; i < 5; ++i) {
        shadhighBox->pack_start(*multipliersh[i]);
    }

    gamFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const gammBox = Gtk::manage(new ToolParamBlock());
    gammBox->pack_start(*gamSH);
    gammBox->pack_start(*sloSH);
    gamFrame->add(*gammBox);

    gradSHFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const gradSHBox = Gtk::manage(new ToolParamBlock());
    gradSHBox->pack_start(*strSH);
    gradSHBox->pack_start(*angSH);
    gradSHFrame->add(*gradSHBox);

    shadhighBox->pack_start(*detailSH);

    if (complexsoft < 2) {
        shadhighBox->pack_start(*gamFrame);
    }

    shadhighBox->pack_start(*highlights);
    shadhighBox->pack_start(*h_tonalwidth);
    shadhighBox->pack_start(*shadows);
    shadhighBox->pack_start(*s_tonalwidth);
    shadhighBox->pack_start(*sh_radius);
    shadhighBox->pack_start(*sensihs);

    if (complexsoft < 2) {
        shadhighBox->pack_start(*blurSHde);
    }

    shadhighBox->pack_start(*gradSHFrame);
    shadhighBox->pack_start(*inverssh);

    fatSHFrame->set_label_align(0.025, 0.5);

//    if (showtooltip) {
//        fatSHFrame->set_tooltip_text(M("TP_LOCALLAB_FATSHFRAME_TOOLTIP"));
//    }


    ToolParamBlock* const fatSHBox = Gtk::manage(new ToolParamBlock());
    fatSHBox->pack_start(*fatamountSH);
    fatSHBox->pack_start(*fatanchorSH);
    fatSHFrame->add(*fatSHBox);

    ToolParamBlock* const maskSHBox = Gtk::manage(new ToolParamBlock());
    maskSHBox->pack_start(*showmaskSHMethod, Gtk::PACK_SHRINK, 4);
    maskSHBox->pack_start(*showmaskSHMethodinv, Gtk::PACK_SHRINK, 4);
    maskSHBox->pack_start(*enaSHMask, Gtk::PACK_SHRINK, 0);
    maskSHBox->pack_start(*maskSHCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    maskSHBox->pack_start(*blendmaskSH, Gtk::PACK_SHRINK, 0);
    maskSHBox->pack_start(*radmaskSH, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        maskSHBox->pack_start(*lapmaskSH, Gtk::PACK_SHRINK, 0);
    }

    maskSHBox->pack_start(*chromaskSH, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        maskSHBox->pack_start(*gammaskSH, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        maskSHBox->pack_start(*slomaskSH, Gtk::PACK_SHRINK, 0);
    }

    maskSHBox->pack_start(*mask2SHCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor

    if (complexsoft < 1) {
        maskSHBox->pack_start(*fatSHFrame);
    }

    expmasksh->add(*maskSHBox, false);
    shadhighBox->pack_start(*expmasksh);


    expshadhigh->add(*shadhighBox, false);
    expshadhigh->setLevel(2);

    panel->pack_start(*expshadhigh, false, false);

    // Vibrance
    expvibrance->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expvibrance));
    enablevibranceConn = expvibrance->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expvibrance));

    setExpandAlignProperties(expmaskvib, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskvib->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskvib));
    expmaskvib->setLevel(2);

    if (showtooltip) {
        expmaskvib->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    saturated->setAdjusterListener(this);

    pastels->setAdjusterListener(this);


    if (showtooltip) {
        psThreshold->set_tooltip_markup(M("TP_VIBRANCE_PSTHRESHOLD_TOOLTIP"));
    }

    psThreshold->setAdjusterListener(this);

    pskinsconn = protectSkins->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::protectskins_toggled));

    ashiftconn = avoidColorShift->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::avoidcolorshift_toggled));

    pastsattogconn = pastSatTog->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::pastsattog_toggled));

    sensiv->setAdjusterListener(this);
    blendmaskvib->setAdjusterListener(this);
    radmaskvib->setLogScale(10, -10);
    radmaskvib->setAdjusterListener(this);
    chromaskvib->setAdjusterListener(this);
    gammaskvib->setAdjusterListener(this);
    slomaskvib->setAdjusterListener(this);
    lapmaskvib->setAdjusterListener(this);
    strvib->setAdjusterListener(this);
    angvib->setAdjusterListener(this);
    strvibab->setAdjusterListener(this);
    strvibh->setAdjusterListener(this);

    if (showtooltip) {
        strvib->set_tooltip_text(M("TP_LOCALLAB_GRADGEN_TOOLTIP"));
        strvibab->set_tooltip_text(M("TP_LOCALLAB_GRADSTRAB_TOOLTIP"));
        strvibh->set_tooltip_text(M("TP_LOCALLAB_GRADSTRHUE_TOOLTIP"));
        angvib->set_tooltip_text(M("TP_LOCALLAB_GRADANG_TOOLTIP"));
    }

    curveEditorGG->setCurveListener(this);

    if (showtooltip) {
        skinTonesCurve->setTooltip(M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_TOOLTIP"));
    }

    const std::vector<GradientMilestone> mskinTonesCurve =
    []() -> std::vector<GradientMilestone> {
        std::vector<GradientMilestone> res;

        float R, G, B;
        Color::hsv2rgb01(0.92f, 0.45f, 0.6f, R, G, B);
        res.emplace_back(0.0, R, G, B);
        Color::hsv2rgb01(0.14056f, 0.45f, 0.6f, R, G, B);
        res.emplace_back(1.0, R, G, B);

        return res;
    }();
    // -0.1 rad < Hue < 1.6 rad
    skinTonesCurve->setBottomBarBgGradient(mskinTonesCurve);
    skinTonesCurve->setLeftBarBgGradient(mskinTonesCurve);
    skinTonesCurve->setRangeLabels(
        M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_RANGE1"), M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_RANGE2"),
        M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_RANGE3"), M("TP_VIBRANCE_CURVEEDITOR_SKINTONES_RANGE4")
    );
    skinTonesCurve->setRangeDefaultMilestones(0.1, 0.4, 0.85);

    curveEditorGG->curveListComplete();

    gradvibFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const gradvibBox = Gtk::manage(new ToolParamBlock());
    gradvibBox->pack_start(*strvib);

    if (complexsoft < 2) {
        gradvibBox->pack_start(*strvibab);
    }

    if (complexsoft < 2) {
        gradvibBox->pack_start(*strvibh);
    }

    gradvibBox->pack_start(*angvib);
    gradvibFrame->add(*gradvibBox);

    ToolParamBlock* const vibranceBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        vibranceBox->pack_start(*saturated, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft == 2) {
        pastels->setLabel(M("TP_LOCALLAB_PASTELS2"));
    }

    vibranceBox->pack_start(*pastels, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        vibranceBox->pack_start(*psThreshold, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        vibranceBox->pack_start(*protectSkins, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        vibranceBox->pack_start(*avoidColorShift, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        vibranceBox->pack_start(*pastSatTog, Gtk::PACK_SHRINK, 0);
    }

    vibranceBox->pack_start(*sensiv, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        vibranceBox->pack_start(*curveEditorGG, Gtk::PACK_SHRINK, 4);    // Padding is mandatory to correct behavior of curve editor
    }

    if (complexsoft < 2) {
        vibranceBox->pack_start(*gradvibFrame, Gtk::PACK_SHRINK, 0);
    }

    enavibMaskConn = enavibMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enavibMaskChanged));

    showmaskvibMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskvibMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskvibMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskvibMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskvibMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));


    showmaskvibMethod->set_active(0);

    if (showtooltip) {
        showmaskvibMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskvibMethodConn  = showmaskvibMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskvibMethodChanged));

    maskvibCurveEditorG->setCurveListener(this);

    CCmaskvibshape->setIdentityValue(0.);
    CCmaskvibshape->setResetCurve(FlatCurveType(defSpot.CCmaskvibcurve.at(0)), defSpot.CCmaskvibcurve);

    if (showtooltip) {
        CCmaskvibshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskvibshape->setBottomBarColorProvider(this, 7);

    LLmaskvibshape->setIdentityValue(0.);
    LLmaskvibshape->setResetCurve(FlatCurveType(defSpot.LLmaskvibcurve.at(0)), defSpot.LLmaskvibcurve);

    if (showtooltip) {
        LLmaskvibshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskvibshape->setBottomBarBgGradient(mllshape);

    HHmaskvibshape->setIdentityValue(0.);
    HHmaskvibshape->setResetCurve(FlatCurveType(defSpot.HHmaskvibcurve.at(0)), defSpot.HHmaskvibcurve);

    if (showtooltip) {
        HHmaskvibshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskvibshape->setCurveColorProvider(this, 6);
    HHmaskvibshape->setBottomBarColorProvider(this, 6);

    maskvibCurveEditorG->curveListComplete();

    mask2vibCurveEditorG->setCurveListener(this);
    Lmaskvibshape->setResetCurve(DiagonalCurveType(defSpot.Lmaskvibcurve.at(0)), defSpot.Lmaskvibcurve);

    if (showtooltip) {
        Lmaskvibshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskvibshape = zero_one_shape;
    Lmaskvibshape->setBottomBarBgGradient(mLmaskvibshape);
    Lmaskvibshape->setLeftBarBgGradient(mLmaskvibshape);
    mask2vibCurveEditorG->curveListComplete();

    ToolParamBlock* const maskvibBox = Gtk::manage(new ToolParamBlock());

    maskvibBox->pack_start(*showmaskvibMethod, Gtk::PACK_SHRINK, 4);
    maskvibBox->pack_start(*enavibMask, Gtk::PACK_SHRINK, 0);
    maskvibBox->pack_start(*maskvibCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    maskvibBox->pack_start(*blendmaskvib, Gtk::PACK_SHRINK, 0);
    maskvibBox->pack_start(*radmaskvib, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        maskvibBox->pack_start(*lapmaskvib, Gtk::PACK_SHRINK, 0);
    }

    maskvibBox->pack_start(*chromaskvib, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        maskvibBox->pack_start(*gammaskvib, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        maskvibBox->pack_start(*slomaskvib, Gtk::PACK_SHRINK, 0);
    }

    maskvibBox->pack_start(*mask2vibCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor

    expmaskvib->add(*maskvibBox, false);
    vibranceBox->pack_start(*expmaskvib);
    expvibrance->add(*vibranceBox, false);
    expvibrance->setLevel(2);

    panel->pack_start(*expvibrance, false, false);

    // Soft Light
    expsoft->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expsoft));
    enablesoftConn = expsoft->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expsoft));
    softMethod->append(M("TP_LOCALLAB_SOFTM"));
    softMethod->append(M("TP_LOCALLAB_RETIM"));
    softMethod->set_active(0);

    if (showtooltip) {
        softMethod->set_tooltip_markup(M("TP_LOCALLAB_SOFTMETHOD_TOOLTIP"));
    }

    softMethodConn = softMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::softMethodChanged));


    Gtk::Label* const labelsoftmethod = Gtk::manage(new Gtk::Label(M("TP_LOCALLAB_SHOWDCT") + ":"));
    ctboxsoftmethod->pack_start(*labelsoftmethod, Gtk::PACK_SHRINK, 4);

    showmasksoftMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmasksoftMethod->append(M("TP_LOCALLAB_SHOWLAPLACE"));
    showmasksoftMethod->append(M("TP_LOCALLAB_SHOWFOURIER"));
    showmasksoftMethod->append(M("TP_LOCALLAB_SHOWPOISSON"));
    showmasksoftMethod->append(M("TP_LOCALLAB_SHOWNORMAL"));

    showmasksoftMethod->set_active(0);

    if (showtooltip) {
        showmasksoftMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKSOFT_TOOLTIP"));
    }

    showmasksoftMethodConn  = showmasksoftMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmasksoftMethodChanged));
    ctboxsoftmethod->pack_start(*showmasksoftMethod);


    streng->setAdjusterListener(this);
    laplace->setAdjusterListener(this);

    sensisf->setAdjusterListener(this);

    ToolParamBlock* const softBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        softBox->pack_start(*softMethod);
    }

//   softBox->pack_start(*showmasksoftMethod, Gtk::PACK_SHRINK, 4);
    softBox->pack_start(*ctboxsoftmethod);
    softBox->pack_start(*streng);
    softBox->pack_start(*laplace);
    softBox->pack_start(*sensisf);
    expsoft->add(*softBox, false);
    expsoft->setLevel(2);

    panel->pack_start(*expsoft, false, false);

    // Tone Mapping
    Gtk::HBox* const TMTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const TMLabel = Gtk::manage(new Gtk::Label());
    TMLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_TM")) + Glib::ustring("</b>"));
    TMLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    TMTitleHBox->pack_start(*TMLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const TMImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        TMImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    TMTitleHBox->pack_end(*TMImage, Gtk::PACK_SHRINK, 0);
    exptonemap->setLabel(TMTitleHBox);
    exptonemap->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), exptonemap));
    enabletonemapConn = exptonemap->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), exptonemap));

    if (showtooltip) {
        exptonemap->set_tooltip_text(M("TP_LOCALLAB_TONEMAP_TOOLTIP"));
    }

    setExpandAlignProperties(expmasktm, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmasktm->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmasktm));
    expmasktm->setLevel(2);

    if (showtooltip) {
        expmasktm->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    stren->setAdjusterListener(this);

    gamma->setAdjusterListener(this);

    estop->setAdjusterListener(this);

    scaltm->setAdjusterListener(this);

    rewei->setAdjusterListener(this);

    amount->setAdjusterListener(this);
    satur->setAdjusterListener(this);

    if (showtooltip) {
        sensitm->set_tooltip_text(M("TP_LOCALLAB_SENSI_TOOLTIP"));
    }

    sensitm->setAdjusterListener(this);
    softradiustm->setLogScale(10, -10);
    softradiustm->setAdjusterListener(this);

    if (showtooltip) {
        estop->set_tooltip_text(M("TP_LOCALLAB_TONEMAPESTOP_TOOLTIP"));
    }

    if (showtooltip) {
        rewei->set_tooltip_text(M("TP_LOCALLAB_TONEMAPESTOP_TOOLTIP"));
    }

    equiltmConn = equiltm->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::equiltmChanged));

    masktmCurveEditorG->setCurveListener(this);

    CCmasktmshape->setIdentityValue(0.);
    CCmasktmshape->setResetCurve(FlatCurveType(defSpot.CCmasktmcurve.at(0)), defSpot.CCmasktmcurve);

    if (showtooltip) {
        CCmasktmshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmasktmshape->setBottomBarColorProvider(this, 7);

    LLmasktmshape->setIdentityValue(0.);
    LLmasktmshape->setResetCurve(FlatCurveType(defSpot.LLmasktmcurve.at(0)), defSpot.LLmasktmcurve);

    if (showtooltip) {
        LLmasktmshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmasktmshape->setBottomBarBgGradient(mllshape);

    HHmasktmshape->setIdentityValue(0.);
    HHmasktmshape->setResetCurve(FlatCurveType(defSpot.HHmasktmcurve.at(0)), defSpot.HHmasktmcurve);

    if (showtooltip) {
        HHmasktmshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmasktmshape->setCurveColorProvider(this, 6);
    HHmasktmshape->setBottomBarColorProvider(this, 6);

    masktmCurveEditorG->curveListComplete();

    mask2tmCurveEditorG->setCurveListener(this);
    Lmasktmshape->setResetCurve(DiagonalCurveType(defSpot.Lmasktmcurve.at(0)), defSpot.Lmasktmcurve);

    if (showtooltip) {
        Lmasktmshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmasktmshape = zero_one_shape;
    Lmasktmshape->setBottomBarBgGradient(mLmasktmshape);
    Lmasktmshape->setLeftBarBgGradient(mLmasktmshape);
    mask2tmCurveEditorG->curveListComplete();

    enatmMaskConn = enatmMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enatmMaskChanged));
    enatmMaskaftConn = enatmMaskaft->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enatmMaskaftChanged));

    showmasktmMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmasktmMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmasktmMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmasktmMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmasktmMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));


    showmasktmMethod->set_active(0);

    if (showtooltip) {
        showmasktmMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmasktmMethodConn  = showmasktmMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmasktmMethodChanged));

    if (showtooltip) {
        radmasktm->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmasktm->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //    chromasktm->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }


    blendmasktm->setAdjusterListener(this);
    radmasktm->setLogScale(10, -10);
    radmasktm->setAdjusterListener(this);
    chromasktm->setAdjusterListener(this);
    gammasktm->setAdjusterListener(this);
    slomasktm->setAdjusterListener(this);
    lapmasktm->setAdjusterListener(this);

    ToolParamBlock* const masktmBox = Gtk::manage(new ToolParamBlock());
    masktmBox->pack_start(*showmasktmMethod, Gtk::PACK_SHRINK, 4);
    masktmBox->pack_start(*enatmMask, Gtk::PACK_SHRINK, 0);
    masktmBox->pack_start(*enatmMaskaft, Gtk::PACK_SHRINK, 0);
    masktmBox->pack_start(*masktmCurveEditorG, Gtk::PACK_SHRINK, 4);
    masktmBox->pack_start(*blendmasktm, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        masktmBox->pack_start(*lapmasktm, Gtk::PACK_SHRINK, 0);
    }

    masktmBox->pack_start(*radmasktm, Gtk::PACK_SHRINK, 0);
    masktmBox->pack_start(*chromasktm, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        masktmBox->pack_start(*gammasktm, Gtk::PACK_SHRINK, 0);
        masktmBox->pack_start(*slomasktm, Gtk::PACK_SHRINK, 0);
    }

    masktmBox->pack_start(*mask2tmCurveEditorG, Gtk::PACK_SHRINK, 4);
    expmasktm->add(*masktmBox, false);


    ToolParamBlock* const tmBox = Gtk::manage(new ToolParamBlock());
//    tmBox->pack_start(*amount);//to use if we change transit_shapedetect parameters
    tmBox->pack_start(*stren);
    tmBox->pack_start(*equiltm);

    if (complexsoft < 2) {
        tmBox->pack_start(*gamma);
        tmBox->pack_start(*satur);
    }

    tmBox->pack_start(*estop);
    tmBox->pack_start(*scaltm);

    if (complexsoft < 2) {
        tmBox->pack_start(*rewei);
    }

//    tmBox->pack_start(*softradiustm);//always bad with TM ??
    tmBox->pack_start(*sensitm);
    tmBox->pack_start(*expmasktm);

    exptonemap->add(*tmBox, false);
    exptonemap->setLevel(2);

//    if (complexsoft < 2) {
    panel->pack_start(*exptonemap, false, false);
//    }

    // Retinex
    Gtk::HBox* const retiTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const retiLabel = Gtk::manage(new Gtk::Label());
    retiLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_RETI")) + Glib::ustring("</b>"));
    retiLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    retiTitleHBox->pack_start(*retiLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const retiImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        retiImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    retiTitleHBox->pack_end(*retiImage, Gtk::PACK_SHRINK, 0);
    expreti->setLabel(retiTitleHBox);
    expreti->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expreti));
    enableretiConn = expreti->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expreti));
    fftwretiConn  = fftwreti->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::fftwretiChanged));

    if (showtooltip) {
        fftwreti->set_tooltip_text(M("TP_LOCALLAB_RETI_FFTW_TOOLTIP"));
    }

    setExpandAlignProperties(expmaskreti, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskreti->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskreti));
    expmaskreti->setLevel(2);

    if (showtooltip) {
        expmaskreti->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    retinexMethod->append(M("TP_RETINEX_LOW"));
    retinexMethod->append(M("TP_RETINEX_UNIFORM"));
    retinexMethod->append(M("TP_RETINEX_HIGH"));
    retinexMethod->set_active(0);

    if (showtooltip) {
        retinexMethod->set_tooltip_markup(M("TP_LOCRETI_METHOD_TOOLTIP"));
    }

    retinexMethodConn = retinexMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::retinexMethodChanged));

    dehaFrame->set_label_align(0.025, 0.5);
    retiFrame->set_label_align(0.025, 0.5);
    retitoolFrame->set_label_align(0.025, 0.5);

    str->setAdjusterListener(this);

//    if (showtooltip) {
//        str->set_tooltip_text(M("TP_LOCALLAB_STRRETI_TOOLTIP"));
//    }

    neigh->setAdjusterListener(this);

    if (showtooltip) {
        neigh->set_tooltip_text(M("TP_LOCALLAB_RETI_NEIGH_VART_TOOLTIP"));
    }

    vart->setAdjusterListener(this);

    if (showtooltip) {
        vart->set_tooltip_text(M("TP_LOCALLAB_RETI_NEIGH_VART_TOOLTIP"));
    }

    offs->setAdjusterListener(this);

    dehaz->setAdjusterListener(this);
    depth->setAdjusterListener(this);

    chrrt->setAdjusterListener(this);

    if (showtooltip) {
        sensih->set_tooltip_text(M("TP_LOCALLAB_SENSIH_TOOLTIP"));
    }

    sensih->setAdjusterListener(this);
    softradiusret->setLogScale(10, -10);
    softradiusret->setAdjusterListener(this);

    if (showtooltip) {
        softradiusret->set_tooltip_text(M("TP_LOCALLAB_SOFTRETI_TOOLTIP"));
    }

    setExpandAlignProperties(mMLabels, true, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);

    if (showtooltip) {
        mMLabels->set_tooltip_markup(M("TP_LOCALLAB_MLABEL_TOOLTIP"));
    }

    setExpandAlignProperties(transLabels, true, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);

    if (showtooltip) {
        transLabels->set_tooltip_markup(M("TP_LOCALLAB_TLABEL_TOOLTIP"));
    }

    setExpandAlignProperties(transLabels2, true, false, Gtk::ALIGN_CENTER, Gtk::ALIGN_START);

    LocalcurveEditortransT->setCurveListener(this);
    cTtransshape->setIdentityValue(0.);
    cTtransshape->setResetCurve(FlatCurveType(defSpot.localTtranscurve.at(0)), defSpot.localTtranscurve);

    if (showtooltip) {
        cTtransshape->setTooltip(M("TP_LOCALLAB_TRANSMISSION_TOOLTIP"));
    }

    LocalcurveEditortransT->curveListComplete();

    LocalcurveEditorgainT->setCurveListener(this);

    cTgainshape->setIdentityValue(0.);
    cTgainshape->setResetCurve(FlatCurveType(defSpot.localTgaincurve.at(0)), defSpot.localTgaincurve);

    if (showtooltip) {
        cTgainshape->setTooltip(M("TP_RETINEX_GAINTRANSMISSION_TOOLTIP"));
    }

    LocalcurveEditorgainT->curveListComplete();

    inversretConn  = inversret->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::inversretChanged));
    equilretConn  = equilret->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::equilretChanged));
    loglinConn  = loglin->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::loglinChanged));
    lumonlyConn  = lumonly->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::lumonlyChanged));

    maskretiCurveEditorG->setCurveListener(this);

    CCmaskretishape->setIdentityValue(0.);
    CCmaskretishape->setResetCurve(FlatCurveType(defSpot.CCmaskreticurve.at(0)), defSpot.CCmaskreticurve);

    if (showtooltip) {
        CCmaskretishape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskretishape->setBottomBarColorProvider(this, 7);

    LLmaskretishape->setIdentityValue(0.);
    LLmaskretishape->setResetCurve(FlatCurveType(defSpot.LLmaskreticurve.at(0)), defSpot.LLmaskreticurve);

    if (showtooltip) {
        LLmaskretishape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskretishape->setBottomBarBgGradient(mllshape);

    HHmaskretishape->setIdentityValue(0.);
    HHmaskretishape->setResetCurve(FlatCurveType(defSpot.HHmaskreticurve.at(0)), defSpot.HHmaskreticurve);

    if (showtooltip) {
        HHmaskretishape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskretishape->setCurveColorProvider(this, 6);
    HHmaskretishape->setBottomBarColorProvider(this, 6);

    maskretiCurveEditorG->curveListComplete();

    mask2retiCurveEditorG->setCurveListener(this);
    Lmaskretishape->setResetCurve(DiagonalCurveType(defSpot.Lmaskreticurve.at(0)), defSpot.Lmaskreticurve);

    if (showtooltip) {
        Lmaskretishape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskretishape = zero_one_shape;
    Lmaskretishape->setBottomBarBgGradient(mLmaskretishape);
    Lmaskretishape->setLeftBarBgGradient(mLmaskretishape);
    mask2retiCurveEditorG->curveListComplete();

    enaretiMaskConn = enaretiMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaretiMaskChanged));
    enaretiMasktmapConn = enaretiMasktmap->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enaretiMasktmapChanged));

    if (showtooltip) {
        enaretiMasktmap->set_tooltip_markup(M("TP_LOCALLAB_ENARETIMASKTMAP_TOOLTIP"));
    }

    showmaskretiMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskretiMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskretiMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskretiMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskretiMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));


    showmaskretiMethod->set_active(0);

    if (showtooltip) {
        showmaskretiMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskretiMethodConn  = showmaskretiMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskretiMethodChanged));

    if (showtooltip) {
        radmaskreti->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskreti->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //    chromaskreti->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }


    blendmaskreti->setAdjusterListener(this);
    radmaskreti->setAdjusterListener(this);
    chromaskreti->setAdjusterListener(this);
    gammaskreti->setAdjusterListener(this);
    slomaskreti->setAdjusterListener(this);
    lapmaskreti->setAdjusterListener(this);
    scalereti->setAdjusterListener(this);
    darkness->setAdjusterListener(this);
    lightnessreti->setAdjusterListener(this);
    limd->setAdjusterListener(this);
    cliptm->setAdjusterListener(this);

    maskretiBox->pack_start(*showmaskretiMethod, Gtk::PACK_SHRINK, 4);
    maskretiBox->pack_start(*enaretiMask, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*enaretiMasktmap, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*maskretiCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    maskretiBox->pack_start(*blendmaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*radmaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*lapmaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*chromaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*gammaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*slomaskreti, Gtk::PACK_SHRINK, 0);
    maskretiBox->pack_start(*mask2retiCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    expmaskreti->add(*maskretiBox, false);
    ToolParamBlock* const auxBox = Gtk::manage(new ToolParamBlock());

    ToolParamBlock* const dehaBox = Gtk::manage(new ToolParamBlock());
    dehaBox->pack_start(*dehaz);
    dehaBox->pack_start(*depth);
    dehaBox->pack_start(*lumonly);
    dehaFrame->add(*dehaBox);

    ToolParamBlock* const deharetiBox = Gtk::manage(new ToolParamBlock());
    deharetiBox->pack_start(*str);
    deharetiBox->pack_start(*loglin);
    retiFrame->add(*deharetiBox);

    auxBox->add(*dehaFrame);

    if (complexsoft < 1) {
        auxBox->add(*retiFrame);
    }

    ToolParamBlock* const scopeBox = Gtk::manage(new ToolParamBlock());
    scopeBox->pack_start(*sensih);
    auxBox->add(*scopeBox);

    ToolParamBlock* const genBox = Gtk::manage(new ToolParamBlock());
    genBox->pack_start(*auxBox);


    retiBox->pack_start(*retinexMethod);
    retiBox->pack_start(*fftwreti);
    retiBox->pack_start(*equilret);
    retiBox->pack_start(*chrrt);
    retiBox->pack_start(*neigh);
    retiBox->pack_start(*vart);
    retiBox->pack_start(*scalereti);
    retiBox->pack_start(*darkness);
    retiBox->pack_start(*lightnessreti);
    retiBox->pack_start(*softradiusret);
    retiBox->pack_start(*limd);
    retiBox->pack_start(*cliptm);
    retiBox->pack_start(*offs);
    retiBox->pack_start(*mMLabels);
    retiBox->pack_start(*transLabels);
    retiBox->pack_start(*transLabels2);
    retiBox->pack_start(*LocalcurveEditortransT, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    retiBox->pack_start(*LocalcurveEditorgainT, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    retiBox->pack_start(*expmaskreti);
//    retiBox->pack_start(*inversret);
    retitoolFrame->add(*retiBox);
    genBox->pack_start(*retitoolFrame);
    expreti->add(*genBox, false);
    expreti->setLevel(2);

    panel->pack_start(*expreti, false, false);

    // Sharpening
    Gtk::HBox* const sharpTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const sharpLabel = Gtk::manage(new Gtk::Label());
    sharpLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_SHARP")) + Glib::ustring("</b>"));
    sharpLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    sharpTitleHBox->pack_start(*sharpLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const sharpImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        sharpImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    sharpTitleHBox->pack_end(*sharpImage, Gtk::PACK_SHRINK, 0);
    expsharp->setLabel(sharpTitleHBox);
    expsharp->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expsharp));
    enablesharpConn = expsharp->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expsharp));

    sharcontrast->setAdjusterListener(this);

    sharradius->setAdjusterListener(this);

    sharamount->setAdjusterListener(this);

    shardamping->setAdjusterListener(this);

    shariter->setAdjusterListener(this);

    sharblur->setAdjusterListener(this);

    if (showtooltip) {
        sensisha->set_tooltip_text(M("TP_LOCALLAB_SENSIS_TOOLTIP"));
    }

    sensisha->setAdjusterListener(this);

    inversshaConn  = inverssha->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::inversshaChanged));

    ToolParamBlock* const sharpBox = Gtk::manage(new ToolParamBlock());
    sharpBox->pack_start(*sharcontrast);
    sharpBox->pack_start(*sharradius);
    sharpBox->pack_start(*sharamount);

    if (complexsoft < 2) {
        sharpBox->pack_start(*shardamping);
    }

    if (complexsoft < 2) {
        sharpBox->pack_start(*shariter);
    }

    if (complexsoft < 2) {
        sharpBox->pack_start(*sharblur);
    }

    sharpBox->pack_start(*sensisha);
    sharpBox->pack_start(*inverssha);
    expsharp->add(*sharpBox, false);
    expsharp->setLevel(2);

    panel->pack_start(*expsharp, false, false);

    // Local Contrast
    expcontrast->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expcontrast));
    enablecontrastConn = expcontrast->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expcontrast));
    fftwlcConn  = fftwlc->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::fftwlcChanged));
    csThreshold->setAdjusterListener(this);

    if (showtooltip) {
        fftwlc->set_tooltip_text(M("TP_LOCALLAB_LC_FFTW_TOOLTIP"));
    }

    LocalcurveEditorwav->setCurveListener(this);

    wavshape->setIdentityValue(0.);
    wavshape->setResetCurve(FlatCurveType(defSpot.locwavcurve.at(0)), defSpot.locwavcurve);

    if (showtooltip) {
//        wavshape->setTooltip(M("TP_RETINEX_WAV_TOOLTIP"));
    }

    LocalcurveEditorwav->curveListComplete();

    localcontMethod->append(M("TP_LOCALLAB_LOCCONT"));
    localcontMethod->append(M("TP_LOCALLAB_WAVE"));
    localcontMethod->set_active(0);

    if (showtooltip) {
        //    localcontMethod->set_tooltip_markup(M("TP_LOCALLAB_LOCMETHOD_TOOLTIP"));
    }

    if (showtooltip) {
        clariFrame->set_tooltip_markup(M("TP_LOCALLAB_CLARI_TOOLTIP"));
    }

    localcontMethodConn = localcontMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::localcontMethodChanged));

    lcradius->setAdjusterListener(this);

    lcamount->setAdjusterListener(this);

    lcdarkness->setAdjusterListener(this);

    lclightness->setAdjusterListener(this);
    levelwav->setAdjusterListener(this);

    if (showtooltip) {
        levelwav->set_tooltip_markup(M("TP_LOCALLAB_LEVELWAV_TOOLTIP"));
    }

    residcont->setAdjusterListener(this);
    residchro->setAdjusterListener(this);
    clarilres->setAdjusterListener(this);
    clarisoft->setAdjusterListener(this);
    claricres->setAdjusterListener(this);

    sensilc->setAdjusterListener(this);

    clariFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const clariBox = Gtk::manage(new ToolParamBlock());
    clariBox->pack_start(*clarilres);

    if (complexsoft < 2) {
        clariBox->pack_start(*claricres);
    }

    if (complexsoft < 2) {
        clariBox->pack_start(*clarisoft);
    }

    clariFrame->add(*clariBox);

    ToolParamBlock* const contrastBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        contrastBox->pack_start(*localcontMethod);
    }

    contrastBox->pack_start(*lcradius);
    contrastBox->pack_start(*lcamount);
    contrastBox->pack_start(*lcdarkness);
    contrastBox->pack_start(*lclightness);
    contrastBox->pack_start(*LocalcurveEditorwav, Gtk::PACK_SHRINK, 4);

//    contrastBox->pack_start(*levelwav);
//    if (complexsoft < 2) {
    contrastBox->pack_start(*csThreshold);
//    }

//   if (complexsoft < 2) {
    contrastBox->pack_start(*residcont);
//   }

    if (complexsoft < 2) {
        contrastBox->pack_start(*residchro);
    }

//    if (complexsoft < 2) {
    contrastBox->pack_start(*clariFrame);
//    }

    contrastBox->pack_start(*sensilc);

    if (complexsoft < 2) {
        contrastBox->pack_start(*fftwlc);
    }

    expcontrast->add(*contrastBox, false);
    expcontrast->setLevel(2);

    panel->pack_start(*expcontrast, false, false);

    // Contrast by detail levels
    Gtk::HBox* const cbdlTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const cbdlLabel = Gtk::manage(new Gtk::Label());
    cbdlLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_CBDL")) + Glib::ustring("</b>"));
    cbdlLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    cbdlTitleHBox->pack_start(*cbdlLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const cbdlImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        cbdlImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    cbdlTitleHBox->pack_end(*cbdlImage, Gtk::PACK_SHRINK, 0);
    expcbdl->setLabel(cbdlTitleHBox);
    expcbdl->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expcbdl));
    enablecbdlConn = expcbdl->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expcbdl));

    if (showtooltip) {
        expcbdl->set_tooltip_text(M("TP_LOCALLAB_EXPCBDL_TOOLTIP"));
    }

    setExpandAlignProperties(expmaskcb, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskcb->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskcb));
    expmaskcb->setLevel(2);

    if (showtooltip) {
        expmaskcb->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    if (showtooltip) {
        chromacbdl->set_tooltip_text(M("TP_LOCALLAB_CHROMACB_TOOLTIP"));
    }

    chromacbdl->setAdjusterListener(this);

    threshold->setAdjusterListener(this);

    if (showtooltip) {
        sensicb->set_tooltip_text(M("TP_LOCALLAB_SENSIH_TOOLTIP"));
    }

    sensicb->setAdjusterListener(this);
    softradiuscb->setLogScale(10, -10);
    softradiuscb->setAdjusterListener(this);
    clarityml->setAdjusterListener(this);
    contresid->setAdjusterListener(this);
    blurcbdl->setAdjusterListener(this);
    blendmaskcb->setAdjusterListener(this);
    radmaskcb->setLogScale(10, -10);
    radmaskcb->setAdjusterListener(this);
    chromaskcb->setAdjusterListener(this);
    gammaskcb->setAdjusterListener(this);
    slomaskcb->setAdjusterListener(this);
    lapmaskcb->setAdjusterListener(this);

    enacbMaskConn = enacbMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enacbMaskChanged));

    showmaskcbMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskcbMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskcbMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskcbMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskcbMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));


    showmaskcbMethod->set_active(0);

    if (showtooltip) {
        showmaskcbMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskcbMethodConn  = showmaskcbMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskcbMethodChanged));

    maskcbCurveEditorG->setCurveListener(this);

    CCmaskcbshape->setIdentityValue(0.);
    CCmaskcbshape->setResetCurve(FlatCurveType(defSpot.CCmaskcbcurve.at(0)), defSpot.CCmaskcbcurve);

    if (showtooltip) {
        CCmaskcbshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskcbshape->setBottomBarColorProvider(this, 7);

    LLmaskcbshape->setIdentityValue(0.);
    LLmaskcbshape->setResetCurve(FlatCurveType(defSpot.LLmaskcbcurve.at(0)), defSpot.LLmaskcbcurve);

    if (showtooltip) {
        LLmaskcbshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskcbshape->setBottomBarBgGradient(mllshape);

    HHmaskcbshape->setIdentityValue(0.);
    HHmaskcbshape->setResetCurve(FlatCurveType(defSpot.HHmaskcbcurve.at(0)), defSpot.HHmaskcbcurve);

    if (showtooltip) {
        HHmaskcbshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskcbshape->setCurveColorProvider(this, 6);
    HHmaskcbshape->setBottomBarColorProvider(this, 6);

    maskcbCurveEditorG->curveListComplete();

    mask2cbCurveEditorG->setCurveListener(this);
    Lmaskcbshape->setResetCurve(DiagonalCurveType(defSpot.Lmaskcbcurve.at(0)), defSpot.Lmaskcbcurve);

    if (showtooltip) {
        Lmaskcbshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskcbshape = zero_one_shape;
    Lmaskcbshape->setBottomBarBgGradient(mLmaskcbshape);
    Lmaskcbshape->setLeftBarBgGradient(mLmaskcbshape);
    mask2cbCurveEditorG->curveListComplete();


    if (showtooltip) {
        radmaskcb->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskcb->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //    chromaskcb->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }


    ToolParamBlock* const cbdlBox = Gtk::manage(new ToolParamBlock());
    Gtk::HBox* const buttonBox = Gtk::manage(new Gtk::HBox(true, 10));
    buttonBox->pack_start(*lumacontrastMinusButton);
    lumacontrastMinusPressedConn = lumacontrastMinusButton->signal_pressed().connect(sigc::mem_fun(*this, &Locallab::lumacontrastMinusPressed));
    buttonBox->pack_start(*lumaneutralButton);
    lumaneutralPressedConn = lumaneutralButton->signal_pressed().connect(sigc::mem_fun(*this, &Locallab::lumaneutralPressed));
    buttonBox->pack_start(*lumacontrastPlusButton);
    lumacontrastPlusPressedConn = lumacontrastPlusButton->signal_pressed().connect(sigc::mem_fun(*this, &Locallab::lumacontrastPlusPressed));
    cbdlBox->pack_start(*buttonBox);

    for (auto adjuster : multiplier) {
        cbdlBox->pack_start(*adjuster);
    }

    ToolParamBlock* const maskcbBox = Gtk::manage(new ToolParamBlock());
    maskcbBox->pack_start(*showmaskcbMethod, Gtk::PACK_SHRINK, 4);
    maskcbBox->pack_start(*enacbMask, Gtk::PACK_SHRINK, 0);
    maskcbBox->pack_start(*maskcbCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    maskcbBox->pack_start(*blendmaskcb, Gtk::PACK_SHRINK, 0);
    maskcbBox->pack_start(*radmaskcb, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        maskcbBox->pack_start(*lapmaskcb, Gtk::PACK_SHRINK, 0);
    }

    maskcbBox->pack_start(*chromaskcb, Gtk::PACK_SHRINK, 0);

//    if (complexsoft < 2) {
    maskcbBox->pack_start(*gammaskcb, Gtk::PACK_SHRINK, 0);
//    }

//    if (complexsoft < 2) {
    maskcbBox->pack_start(*slomaskcb, Gtk::PACK_SHRINK, 0);
//    }

    maskcbBox->pack_start(*mask2cbCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor
    expmaskcb->add(*maskcbBox, false);

    Gtk::HSeparator* const separator = Gtk::manage(new  Gtk::HSeparator());
    cbdlBox->pack_start(*separator, Gtk::PACK_SHRINK, 2);

//    if (complexsoft < 2) {
    cbdlBox->pack_start(*chromacbdl);
//    }

    cbdlBox->pack_start(*threshold);

//    if (complexsoft < 2) {
    cbdlBox->pack_start(*blurcbdl);
//    }

    residFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const residBox = Gtk::manage(new ToolParamBlock());
    residBox->pack_start(*clarityml);
    residBox->pack_start(*contresid);
    residFrame->add(*residBox);

//    if (complexsoft < 2) {
    cbdlBox->pack_start(*residFrame);
//    }

//    if (complexsoft < 2) {
    cbdlBox->pack_start(*softradiuscb);
//    }

    cbdlBox->pack_start(*sensicb);
    cbdlBox->pack_start(*expmaskcb);
    expcbdl->add(*cbdlBox, false);
    expcbdl->setLevel(2);

    if (complexsoft < 2) {
        panel->pack_start(*expcbdl, false, false);
    }

    // Blur & Noise
    Gtk::HBox* const BLTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const BLLabel = Gtk::manage(new Gtk::Label());
    BLLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_BLUFR")) + Glib::ustring("</b>"));
    BLLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    BLTitleHBox->pack_start(*BLLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const BLImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        BLImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    BLTitleHBox->pack_end(*BLImage, Gtk::PACK_SHRINK, 0);
    expblur->setLabel(BLTitleHBox);

    expblur->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expblur));
    enableblurConn = expblur->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expblur));
    fftwblConn  = fftwbl->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::fftwblChanged));
    toolblConn  = toolbl->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::toolblChanged));

    blMethod->append(M("TP_LOCALLAB_BLUR"));
    blMethod->append(M("TP_LOCALLAB_BLMED"));
    blMethod->append(M("TP_LOCALLAB_BLGUID"));
    blMethod->set_active(0);

    if (showtooltip) {
//        blMethod->set_tooltip_markup(M("TP_LOCALLAB_BLUMETHOD_TOOLTIP"));
        expblur->set_tooltip_markup(M("TP_LOCALLAB_BLUMETHOD_TOOLTIP"));
    }

    blMethodConn = blMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::blMethodChanged));


    radius->setAdjusterListener(this);

    strength->setAdjusterListener(this);

    if (showtooltip) {
        sensibn->set_tooltip_text(M("TP_LOCALLAB_SENSIH_TOOLTIP"));
    }

    sensibn->setAdjusterListener(this);
    isogr->setAdjusterListener(this);
    strengr->setAdjusterListener(this);
    scalegr->setAdjusterListener(this);

    itera->setAdjusterListener(this);
    guidbl->setAdjusterListener(this);
    epsbl->setAdjusterListener(this);

    setExpandAlignProperties(expmaskbl, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_START);
    expmaskbl->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expmaskbl));
    expmaskbl->setLevel(2);

    if (showtooltip) {
        expmaskbl->set_tooltip_markup(M("TP_LOCALLAB_MASK_TOOLTIP"));
    }

    maskblCurveEditorG->setCurveListener(this);

    CCmaskblshape->setIdentityValue(0.);
    CCmaskblshape->setResetCurve(FlatCurveType(defSpot.CCmaskblcurve.at(0)), defSpot.CCmaskblcurve);

    if (showtooltip) {
        CCmaskblshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    CCmaskblshape->setBottomBarColorProvider(this, 7);

    LLmaskblshape->setIdentityValue(0.);
    LLmaskblshape->setResetCurve(FlatCurveType(defSpot.LLmaskblcurve.at(0)), defSpot.LLmaskblcurve);

    if (showtooltip) {
        LLmaskblshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    LLmaskblshape->setBottomBarBgGradient(mllshape);

    HHmaskblshape->setIdentityValue(0.);
    HHmaskblshape->setResetCurve(FlatCurveType(defSpot.HHmaskblcurve.at(0)), defSpot.HHmaskblcurve);

    if (showtooltip) {
        HHmaskblshape->setTooltip(M("TP_LOCALLAB_CURVEEDITOR_CC_TOOLTIP"));
    }

    HHmaskblshape->setCurveColorProvider(this, 6);
    HHmaskblshape->setBottomBarColorProvider(this, 6);

    maskblCurveEditorG->curveListComplete();

    mask2blCurveEditorG->setCurveListener(this);
    Lmaskblshape->setResetCurve(DiagonalCurveType(defSpot.Lmaskblcurve.at(0)), defSpot.Lmaskblcurve);

    if (showtooltip) {
        Lmaskblshape->setTooltip(M("TP_LOCALLAB_LMASK_LL_TOOLTIP"));
    }

    const std::vector<GradientMilestone>& mLmaskblshape = zero_one_shape;
    Lmaskblshape->setBottomBarBgGradient(mLmaskblshape);
    Lmaskblshape->setLeftBarBgGradient(mLmaskblshape);
    mask2blCurveEditorG->curveListComplete();
    csThresholdblur->setAdjusterListener(this);

    mask2blCurveEditorGwav->setCurveListener(this);
    LLmaskblshapewav->setIdentityValue(0.);
    LLmaskblshapewav->setResetCurve(FlatCurveType(defSpot.LLmaskblcurvewav.at(0)), defSpot.LLmaskblcurvewav);

    if (showtooltip) {
        LLmaskblshapewav->setTooltip(M("TP_LOCALLAB_LMASK_LEVEL_TOOLTIP"));
    }

    LLmaskblshapewav->setBottomBarBgGradient(mllshape);


    mask2blCurveEditorGwav->curveListComplete();



    enablMaskConn = enablMask->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::enablMaskChanged));

    showmaskblMethod->append(M("TP_LOCALLAB_SHOWMNONE"));
    showmaskblMethod->append(M("TP_LOCALLAB_SHOWMODIF"));
    showmaskblMethod->append(M("TP_LOCALLAB_SHOWMODIFMASK"));
    showmaskblMethod->append(M("TP_LOCALLAB_SHOWMASK"));
    showmaskblMethod->append(M("TP_LOCALLAB_PREVIEWSEL"));


    showmaskblMethod->set_active(0);

    if (showtooltip) {
        showmaskblMethod->set_tooltip_markup(M("TP_LOCALLAB_SHOWMASKCOL_TOOLTIP"));
    }

    showmaskblMethodConn  = showmaskblMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::showmaskblMethodChanged));

    if (showtooltip) {
        radmaskbl->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        lapmaskbl->set_tooltip_text(M("TP_LOCALLAB_LAPRAD_TOOLTIP"));
        //   chromaskbl->set_tooltip_text(M("TP_LOCALLAB_CHROMASK_TOOLTIP"));
    }

    medMethod->append(M("TP_LOCALLAB_MEDNONE"));
    medMethod->append(M("TP_DIRPYRDENOISE_TYPE_3X3"));
    medMethod->append(M("TP_DIRPYRDENOISE_TYPE_5X5"));
    medMethod->append(M("TP_DIRPYRDENOISE_TYPE_7X7"));
    medMethod->append(M("TP_DIRPYRDENOISE_TYPE_9X9"));

    medMethod->set_active(0);

    if (showtooltip) {
//        medMethod->set_tooltip_markup(M("TP_LOCALLAB_MEDMETHOD_TOOLTIP"));
    }

    medMethodConn = medMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::medMethodChanged));


    blurMethod->append(M("TP_LOCALLAB_BLNORM"));
    blurMethod->append(M("TP_LOCALLAB_BLINV"));
    blurMethod->set_active(0);

    if (showtooltip) {
        blurMethod->set_tooltip_markup(M("TP_LOCALLAB_BLMETHOD_TOOLTIP"));
    }

    blurMethodConn = blurMethod->signal_changed().connect(sigc::mem_fun(*this, &Locallab::blurMethodChanged));

    activlumConn  = activlum->signal_toggled().connect(sigc::mem_fun(*this, &Locallab::activlumChanged));
    blendmaskbl->setAdjusterListener(this);
    radmaskbl->setAdjusterListener(this);
    chromaskbl->setAdjusterListener(this);
    gammaskbl->setAdjusterListener(this);
    slomaskbl->setAdjusterListener(this);
    lapmaskbl->setAdjusterListener(this);
    shadmaskbl->setAdjusterListener(this);
    strumaskbl->setAdjusterListener(this);
    Gtk::HSeparator* const separatorstrubl = Gtk::manage(new  Gtk::HSeparator());

    toolblFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const toolblBox = Gtk::manage(new ToolParamBlock());

    ToolParamBlock* const maskblBox = Gtk::manage(new ToolParamBlock());
    maskblBox->pack_start(*showmaskblMethod, Gtk::PACK_SHRINK, 4);
    maskblBox->pack_start(*enablMask, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        maskblBox->pack_start(*maskblCurveEditorG, Gtk::PACK_SHRINK, 4);    // Padding is mandatory to correct behavior of curve editor
    }

    if (complexsoft < 2) {
        maskblBox->pack_start(*strumaskbl, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        maskblBox->pack_start(*toolbl, Gtk::PACK_SHRINK, 0);
    }

    maskblBox->pack_start(*separatorstrubl, Gtk::PACK_SHRINK, 2);
    maskblBox->pack_start(*blendmaskbl, Gtk::PACK_SHRINK, 0);
    toolblBox->pack_start(*radmaskbl, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 1) {
        toolblBox->pack_start(*lapmaskbl, Gtk::PACK_SHRINK, 0);
    }

    toolblBox->pack_start(*chromaskbl, Gtk::PACK_SHRINK, 0);

    if (complexsoft < 2) {
        toolblBox->pack_start(*gammaskbl, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        toolblBox->pack_start(*slomaskbl, Gtk::PACK_SHRINK, 0);
    }

    if (complexsoft < 2) {
        toolblBox->pack_start(*shadmaskbl, Gtk::PACK_SHRINK, 0);
    }

    toolblBox->pack_start(*mask2blCurveEditorG, Gtk::PACK_SHRINK, 4); // Padding is mandatory to correct behavior of curve editor

    if (complexsoft < 1) {
        toolblBox->pack_start(*mask2blCurveEditorGwav, Gtk::PACK_SHRINK, 4);    // Padding is mandatory to correct behavior of curve editor
    }

    if (complexsoft < 1) {
        toolblBox->pack_start(*csThresholdblur, Gtk::PACK_SHRINK, 0);
    }

    toolblFrame->add(*toolblBox);
    maskblBox->pack_start(*toolblFrame);

    expmaskbl->add(*maskblBox, false);
    panel->pack_start(*expmaskbl);


    grainFrame->set_label_align(0.025, 0.5);
    ToolParamBlock* const grainBox = Gtk::manage(new ToolParamBlock());
    grainBox->pack_start(*isogr);
    grainBox->pack_start(*strengr);
    grainBox->pack_start(*scalegr);

    grainFrame->add(*grainBox);



    ToolParamBlock* const blurrBox = Gtk::manage(new ToolParamBlock());

    if (complexsoft < 2) {
        blurrBox->pack_start(*blMethod);
    }

    if (complexsoft < 2) {
        blurrBox->pack_start(*fftwbl, Gtk::PACK_SHRINK, 0);
    }

    blurrBox->pack_start(*radius);
    blurrBox->pack_start(*strength);
    blurrBox->pack_start(*grainFrame);
    blurrBox->pack_start(*medMethod);
    blurrBox->pack_start(*itera);
    blurrBox->pack_start(*guidbl);
    blurrBox->pack_start(*epsbl);
    blurrBox->pack_start(*sensibn);
    blurrBox->pack_start(*blurMethod);
    blurrBox->pack_start(*activlum);
//    blurrBox->pack_start(*expmaskbl);
    expblur->add(*blurrBox, false);
    expblur->setLevel(2);

    panel->pack_start(*expblur, false, false);



    // Denoise
    Gtk::HBox* const denoiTitleHBox = Gtk::manage(new Gtk::HBox());
    Gtk::Label* const denoiLabel = Gtk::manage(new Gtk::Label());
    denoiLabel->set_markup(Glib::ustring("<b>") + escapeHtmlChars(M("TP_LOCALLAB_DENOIS")) + Glib::ustring("</b>"));
    denoiLabel->set_alignment(Gtk::ALIGN_START, Gtk::ALIGN_CENTER);
    denoiTitleHBox->pack_start(*denoiLabel, Gtk::PACK_EXPAND_WIDGET, 0);
    RTImage* const denoiImage = Gtk::manage(new RTImage("one-to-one-small.png"));

    if (showtooltip) {
        denoiImage->set_tooltip_text(M("TP_GENERAL_11SCALE_TOOLTIP"));
    }

    denoiTitleHBox->pack_end(*denoiImage, Gtk::PACK_SHRINK, 0);
    expdenoi->setLabel(denoiTitleHBox);
    expdenoi->signal_button_release_event().connect_notify(sigc::bind(sigc::mem_fun(this, &Locallab::foldAllButMe), expdenoi));
    enabledenoiConn = expdenoi->signal_enabled_toggled().connect(sigc::bind(sigc::mem_fun(this, &Locallab::enableToggled), expdenoi));

    noiselumf->setAdjusterListener(this);
    noiselumf0->setAdjusterListener(this);
    noiselumf2->setAdjusterListener(this);

    if (showtooltip) {
        noiselumc->set_tooltip_text(M("TP_LOCALLAB_NOISECHROC_TOOLTIP"));
    }

    noiselumc->setAdjusterListener(this);

    noiselumdetail->setAdjusterListener(this);

    if (showtooltip) {
//        noiselumdetail->set_tooltip_text(M("TP_LOCALLAB_NOISEDETAIL_TOOLTIP"));
    }

    noiselequal->setAdjusterListener(this);

    noisechrof->setAdjusterListener(this);

    if (showtooltip) {
        noisechroc->set_tooltip_text(M("TP_LOCALLAB_NOISECHROC_TOOLTIP"));
    }

    noisechroc->setAdjusterListener(this);

    noisechrodetail->setAdjusterListener(this);

    if (showtooltip) {
//        noisechrodetail->set_tooltip_text(M("TP_LOCALLAB_NOISEDETAIL_TOOLTIP"));
    }

    adjblur->setAdjusterListener(this);

    bilateral->setAdjusterListener(this);

    sensiden->setAdjusterListener(this);
    detailthr->setAdjusterListener(this);

    ToolParamBlock* const denoisBox = Gtk::manage(new ToolParamBlock());
    Gtk::Frame* const wavFrame = Gtk::manage(new Gtk::Frame());
    ToolParamBlock* const wavBox = Gtk::manage(new ToolParamBlock());
    wavBox->pack_start(*noiselumf0);
    wavBox->pack_start(*noiselumf);
    wavBox->pack_start(*noiselumf2);
    wavBox->pack_start(*noiselumc);
    wavBox->pack_start(*noiselumdetail);
    wavBox->pack_start(*noiselequal);
    wavBox->pack_start(*noisechrof);
    wavBox->pack_start(*noisechroc);
    wavBox->pack_start(*noisechrodetail);
    wavBox->pack_start(*detailthr);
    wavBox->pack_start(*adjblur);
    wavFrame->add(*wavBox);
    denoisBox->pack_start(*wavFrame);
    denoisBox->pack_start(*bilateral);
    denoisBox->pack_start(*sensiden);
    expdenoi->add(*denoisBox, false);
    expdenoi->setLevel(2);

    if (complexsoft < 2) {
        panel->pack_start(*expdenoi, false, false);
    }

    pack_start(*panel);

    setParamEditable(false);



    show_all();
}

Locallab::~Locallab()
{
    idle_register.destroy();

    delete llCurveEditorG;
    delete clCurveEditorG;
    delete HCurveEditorG;
    delete rgbCurveEditorG;
    delete maskCurveEditorG;
    delete maskHCurveEditorG;
    delete mask2CurveEditorG;
    delete mask2CurveEditorGwav;
    delete mask2expCurveEditorG;
    delete mask2SHCurveEditorG;
    delete mask2tmCurveEditorG;
    delete curveEditorG;
    delete maskexpCurveEditorG;
    delete maskSHCurveEditorG;
    delete maskvibCurveEditorG;
    delete curveEditorGG;
    delete LocalcurveEditortransT;
    delete LocalcurveEditorgainT;
    delete LocalcurveEditorwav;
    delete masktmCurveEditorG;
    delete maskblCurveEditorG;
    delete mask2blCurveEditorG;
    delete mask2blCurveEditorGwav;
    delete maskretiCurveEditorG;
    delete mask2retiCurveEditorG;
    delete maskcbCurveEditorG;
    delete mask2cbCurveEditorG;
}

void Locallab::foldAllButMe(GdkEventButton* event, MyExpander *expander)
{
    if (event->button == 3) {
        expsettings->setExpanded(expsettings->getExpander() == expander);
        expcolor->set_expanded(expcolor == expander);
        expexpose->set_expanded(expexpose == expander);
        expshadhigh->set_expanded(expshadhigh == expander);
        expvibrance->set_expanded(expvibrance == expander);
        expsoft->set_expanded(expsoft == expander);
        expblur->set_expanded(expblur == expander);
        exptonemap->set_expanded(exptonemap == expander);
        expreti->set_expanded(expreti == expander);
        expsharp->set_expanded(expsharp == expander);
        expcontrast->set_expanded(expcontrast == expander);
        expcbdl->set_expanded(expcbdl == expander);
        expdenoi->set_expanded(expdenoi == expander);
        expmaskcol->set_expanded(expmaskcol == expander);
        expmaskcol1->set_expanded(expmaskcol1 == expander);
        expmaskexp->set_expanded(expmaskexp == expander);
        expmasksh->set_expanded(expmasksh == expander);
        expmaskcb->set_expanded(expmaskcb == expander);
        expmaskreti->set_expanded(expmaskreti == expander);
        expmasktm->set_expanded(expmasktm == expander);
        expmaskbl->set_expanded(expmaskbl == expander);
        expmaskvib->set_expanded(expmaskvib == expander);

    }
}

void Locallab::enableToggled(MyExpander *expander)
{
    // printf("enableToggled\n");

    rtengine::ProcEvent event;
    sigc::connection* expConn;

    if (expander == expcolor) {
        event = EvLocenacolor;
        expConn = &enablecolorConn;
    } else if (expander == expexpose) {
        event = EvLocenaexpose;
        expConn = &enableexposeConn;
    } else if (expander == expshadhigh) {
        event = EvLocenashadhigh;
        expConn = &enableshadhighConn;
    } else if (expander == expvibrance) {
        event = EvLocenavibrance;
        expConn = &enablevibranceConn;
    } else if (expander == expsoft) {
        event = EvLocenasoft;
        expConn = &enablesoftConn;
    } else if (expander == expblur) {
        event = EvLocenablur;
        expConn = &enableblurConn;
    } else if (expander == exptonemap) {
        event = EvLocenatonemap;
        expConn = &enabletonemapConn;
    } else if (expander == expreti) {
        event = EvLocenareti;
        expConn = &enableretiConn;
    } else if (expander == expsharp) {
        event = EvLocenasharp;
        expConn = &enablesharpConn;
    } else if (expander == expcontrast) {
        event = EvLocenacontrast;
        expConn = &enablecontrastConn;
    } else if (expander == expcbdl) {
        event = EvLocenacbdl;
        expConn = &enablecbdlConn;
    } else if (expander == expdenoi) {
        event = EvLocenadenoi;
        expConn = &enabledenoiConn;
    } else {
        return;
    }

    if (multiImage) {
        if (expander->get_inconsistent()) {
            expander->set_inconsistent(false);
            expConn->block(true);
            expander->setEnabled(false);
            expConn->block(false);
        }
    }

    if (getEnabled()) {
        if (listener) {
            if (expander->getEnabled()) {
                listener->panelChanged(event, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(event, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::writeOptions(std::vector<int> &tpOpen)
{
    tpOpen.push_back(expsettings->getExpanded());
    tpOpen.push_back(expcolor->get_expanded());
    tpOpen.push_back(expexpose->get_expanded());
    tpOpen.push_back(expshadhigh->get_expanded());
    tpOpen.push_back(expvibrance->get_expanded());
    tpOpen.push_back(expsoft->get_expanded());
    tpOpen.push_back(expblur->get_expanded());
    tpOpen.push_back(exptonemap->get_expanded());
    tpOpen.push_back(expreti->get_expanded());
    tpOpen.push_back(expsharp->get_expanded());
    tpOpen.push_back(expcontrast->get_expanded());
    tpOpen.push_back(expcbdl->get_expanded());
    tpOpen.push_back(expdenoi->get_expanded());
    tpOpen.push_back(expmaskcol->get_expanded());
    tpOpen.push_back(expmaskcol1->get_expanded());
    tpOpen.push_back(expmaskexp->get_expanded());
    tpOpen.push_back(expmasksh->get_expanded());
    tpOpen.push_back(expmaskcb->get_expanded());
    tpOpen.push_back(expmaskreti->get_expanded());
    tpOpen.push_back(expmasktm->get_expanded());
    tpOpen.push_back(expmaskbl->get_expanded());
    tpOpen.push_back(expmaskvib->get_expanded());

}

void Locallab::minmaxChanged(double cdma, double cdmin, double mini, double maxi, double Tmean, double Tsigma, double Tmin, double Tmax)
{
    nextmin = cdmin;
    nextmax = cdma;
    nextminiT = mini;
    nextmaxiT = maxi;
    nextmeanT = Tmean;
    nextsigma = Tsigma;
    nextminT = Tmin;
    nextmaxT = Tmax;

    idle_register.add(
    [this]() -> bool {
        GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected
        // FIXME: The above can't be true?!
        disableListener();
        enableListener();
        updateLabel();
        updateTrans();
        return false;
    }
    );
}

void Locallab::updateLabel()
{
    if (!batchMode) {
        float nX, nY;
        nX = nextmin;
        nY = nextmax;
        {
            mMLabels->set_text(
                Glib::ustring::compose(M("TP_LOCALLAB_MLABEL"),
                                       Glib::ustring::format(std::fixed, std::setprecision(0), nX),
                                       Glib::ustring::format(std::fixed, std::setprecision(0), nY))
            );
        }
    }
}

void Locallab::updateTrans()
{
    if (!batchMode) {
        float nm, nM, nZ, nA, nB, nS;
        nm = nextminiT;
        nM = nextmaxiT;
        nZ = nextmeanT;
        nA = nextminT;
        nB = nextmaxT;
        nS = nextsigma;
        {
            transLabels->set_text(
                Glib::ustring::compose(M("TP_LOCALLAB_TLABEL"),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nm),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nM),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nZ),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nS))
            );
            transLabels2->set_text(
                Glib::ustring::compose(M("TP_RETINEX_TLABEL2"),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nA),
                                       Glib::ustring::format(std::fixed, std::setprecision(1), nB))
            );


        }
    }
}




void Locallab::refChanged(double huer, double lumar, double chromar)
{
    if (!batchMode) {
        // Hue reference normalization (between 0 and 1)
        double normHuer = huer;
        float h = Color::huelab_to_huehsv2(normHuer);
        h += 1.f / 6.f;

        if (h > 1.f) {
            h -= 1.f;
        }

        normHuer = h;

        // Luma reference normalization (between 0 and 1)
        double normLumar = lumar / 100.f;

        // Chroma reference normalization (between 0 and 1)
        double normChromar = chromar / 137.4f;

        // printf("nh=%f nl=%f nc=%f\n", normHuer, normLumar, normChromar);

        idle_register.add(
        [this, normHuer, normLumar, normChromar]() -> bool {
            GThreadLock lock; // All GUI access from idle_add callbacks or separate thread HAVE to be protected

            // Update Color & Light mask background
            CCmaskshape->updateLocallabBackground(normChromar);
            LLmaskshape->updateLocallabBackground(normLumar);
            HHmaskshape->updateLocallabBackground(normHuer);
            HHhmaskshape->updateLocallabBackground(normHuer);

            // Update Exposure mask background
            CCmaskexpshape->updateLocallabBackground(normChromar);
            LLmaskexpshape->updateLocallabBackground(normLumar);
            HHmaskexpshape->updateLocallabBackground(normHuer);

            // Update Shadow Highlight mask background
            CCmaskSHshape->updateLocallabBackground(normChromar);
            LLmaskSHshape->updateLocallabBackground(normLumar);
            HHmaskSHshape->updateLocallabBackground(normHuer);

            // Update Vibrance mask background
            CCmaskvibshape->updateLocallabBackground(normChromar);
            LLmaskvibshape->updateLocallabBackground(normLumar);
            HHmaskvibshape->updateLocallabBackground(normHuer);

            // Update CBDL mask background
            CCmaskcbshape->updateLocallabBackground(normChromar);
            LLmaskcbshape->updateLocallabBackground(normLumar);
            HHmaskcbshape->updateLocallabBackground(normHuer);

            //updtae blur mask background
            CCmaskblshape->updateLocallabBackground(normChromar);
            LLmaskblshape->updateLocallabBackground(normLumar);
            HHmaskblshape->updateLocallabBackground(normHuer);


            // Update TM mask background
            CCmasktmshape->updateLocallabBackground(normChromar);
            LLmasktmshape->updateLocallabBackground(normLumar);
            HHmasktmshape->updateLocallabBackground(normHuer);

            // Update Retinex mask background
            CCmaskretishape->updateLocallabBackground(normChromar);
            LLmaskretishape->updateLocallabBackground(normLumar);
            HHmaskretishape->updateLocallabBackground(normHuer);

            return false;
        }
        );
    }
}

void Locallab::updateToolState(std::vector<int> &tpOpen)
{
    if (tpOpen.size() >= 23) {
        expsettings->setExpanded(tpOpen.at(0));
        expcolor->set_expanded(tpOpen.at(1));
        expexpose->set_expanded(tpOpen.at(2));
        expshadhigh->set_expanded(tpOpen.at(3));
        expvibrance->set_expanded(tpOpen.at(4));
        expsoft->set_expanded(tpOpen.at(5));
        expblur->set_expanded(tpOpen.at(6));
        exptonemap->set_expanded(tpOpen.at(7));
        expreti->set_expanded(tpOpen.at(8));
        expsharp->set_expanded(tpOpen.at(9));
        expcontrast->set_expanded(tpOpen.at(10));
        expcbdl->set_expanded(tpOpen.at(11));
        expdenoi->set_expanded(tpOpen.at(12));
        expmaskcol->set_expanded(tpOpen.at(13));
        expmaskcol1->set_expanded(tpOpen.at(14));
        expmaskexp->set_expanded(tpOpen.at(15));
        expmasksh->set_expanded(tpOpen.at(16));
        expmaskcb->set_expanded(tpOpen.at(17));
        expmaskreti->set_expanded(tpOpen.at(18));
        expmasktm->set_expanded(tpOpen.at(19));
        expmaskbl->set_expanded(tpOpen.at(20));
        expmaskvib->set_expanded(tpOpen.at(21));
    }
}

void Locallab::lumaneutralPressed()
{
    // printf("lumaneutralPressed\n");

    for (int i = 0; i < 6; i++) {
        multiplier[i]->setValue(1.0);
    }

    // Raise event (only for first multiplier because associated event concerns all multipliers)
    adjusterChanged(multiplier[0], multiplier[0]->getValue()); // Value isn't used
}

void Locallab::lumacontrastPlusPressed()
{
    // printf("lumacontrastPlusPressed\n");

    for (int i = 0; i < 6; i++) {
        float inc = (5 - i);
        multiplier[i]->setValue(multiplier[i]->getValue() + 0.01f * inc);
    }

    // Raise event (only for first multiplier because associated event concerns all multipliers)
    adjusterChanged(multiplier[0], multiplier[0]->getValue()); // Value isn't used
}

void Locallab::lumacontrastMinusPressed()
{
    // printf("lumacontrastMinusPressed\n");

    for (int i = 0; i < 6; i++) {
        float inc = - (5 - i);
        multiplier[i]->setValue(multiplier[i]->getValue() + 0.01f * inc);
    }

    // Raise event (only for first multiplier because associated event concerns all multipliers)
    adjusterChanged(multiplier[0], multiplier[0]->getValue()); // Value isn't used
}

void Locallab::read(const rtengine::procparams::ProcParams* pp, const ParamsEdited* pedited)
{
    // printf("Locallab read\n");

    // Disable all listeners
    disableListener();

    setEnabled(pp->locallab.enabled);

    if (pedited) {
        set_inconsistent(multiImage && !pedited->locallab.enabled);
    }

    // Delete all existent spots
    std::vector<int>* const list = expsettings->getSpotIdList();

    for (size_t i = 0; i < list->size(); i++) {
        expsettings->deleteControlSpot(list->at(i));
    }

    // Add existent spots based on pp
    ControlSpotPanel::SpotRow* const r = new ControlSpotPanel::SpotRow();

    for (int i = 0; i < pp->locallab.nbspot && i < (int)pp->locallab.spots.size(); i++) {
        r->id = pp->locallab.spots.at(i).id;
        r->name = pp->locallab.spots.at(i).name;
        r->isvisible = pp->locallab.spots.at(i).isvisible;

        if (pp->locallab.spots.at(i).shape == "ELI") {
            r->shape = 0;
        } else {
            r->shape = 1;
        }

        if (pp->locallab.spots.at(i).spotMethod == "norm") {
            r->spotMethod = 0;
        } else {
            r->spotMethod = 1;
        }

        /*
                if (pp->locallab.spots.at(i).mergeMethod == "none") {
                    r->mergeMethod = 0;
                    expmaskcol1->hide();
                    expmaskcol->show();

                } else if (pp->locallab.spots.at(i).mergeMethod == "short") {
                    r->mergeMethod = 1;
                    expmaskcol1->hide();
                    expmaskcol->show();

                } else if (pp->locallab.spots.at(i).mergeMethod == "orig") {
                    r->mergeMethod = 2;
                    expmaskcol1->show();
                    expmaskcol->hide();

                } else if (pp->locallab.spots.at(i).mergeMethod == "origmas") {
                    r->mergeMethod = 3;
                    expmaskcol1->show();
                    expmaskcol->show();

                } else if (pp->locallab.spots.at(i).mergeMethod == "lastspot") {
                    r->mergeMethod = 4;
                    expmaskcol1->show();
                    expmaskcol->hide();

                } else if (pp->locallab.spots.at(i).mergeMethod == "lastspotmas") {
                    r->mergeMethod = 5;
                    expmaskcol1->show();
                    expmaskcol->hide();

                }
        */
        r->sensiexclu = pp->locallab.spots.at(i).sensiexclu;
        r->structexclu = pp->locallab.spots.at(i).structexclu;
        r->struc = pp->locallab.spots.at(i).struc;

        if (pp->locallab.spots.at(i).shapeMethod == "IND") {
            r->shapeMethod = 0;
        } else if (pp->locallab.spots.at(i).shapeMethod == "SYM") {
            r->shapeMethod = 1;
        } else if (pp->locallab.spots.at(i).shapeMethod == "INDSL") {
            r->shapeMethod = 2;
        } else {
            r->shapeMethod = 3;
        }

        r->locX = pp->locallab.spots.at(i).locX;
        r->locXL = pp->locallab.spots.at(i).locXL;
        r->locY = pp->locallab.spots.at(i).locY;
        r->locYT = pp->locallab.spots.at(i).locYT;
        r->centerX = pp->locallab.spots.at(i).centerX;
        r->centerY = pp->locallab.spots.at(i).centerY;
        r->circrad = pp->locallab.spots.at(i).circrad;

        if (pp->locallab.spots.at(i).qualityMethod == "enh") {
            r->qualityMethod = 0;
        } else {
            r->qualityMethod = 1;
        }

        if (pp->locallab.spots.at(i).complexMethod == "sim") {
            r->complexMethod = 0;
        } else  if (pp->locallab.spots.at(i).complexMethod == "mod") {
            r->complexMethod = 1;
        } else  if (pp->locallab.spots.at(i).complexMethod == "all") {
            r->complexMethod = 2;
        }

        r->transit = pp->locallab.spots.at(i).transit;
        r->feather = pp->locallab.spots.at(i).feather;
        r->thresh = pp->locallab.spots.at(i).thresh;
        r->iter = pp->locallab.spots.at(i).iter;
        r->balan = pp->locallab.spots.at(i).balan;
        r->transitweak = pp->locallab.spots.at(i).transitweak;
        r->transitgrad = pp->locallab.spots.at(i).transitgrad;
        r->scopemask = pp->locallab.spots.at(i).scopemask;
        r->lumask = pp->locallab.spots.at(i).lumask;
        r->avoid = pp->locallab.spots.at(i).avoid;
        r->recurs = pp->locallab.spots.at(i).recurs;
        r->laplac = pp->locallab.spots.at(i).laplac;
        r->deltae = pp->locallab.spots.at(i).deltae;
        r->shortc = pp->locallab.spots.at(i).shortc;
        r->savrest = pp->locallab.spots.at(i).savrest;

        expsettings->addControlSpot(r);
    }

    // Select active spot
    if (pp->locallab.nbspot > 0) {
        expsettings->setSelectedSpot(pp->locallab.spots.at(pp->locallab.selspot).id);
    }

    // Update Locallab tools GUI
    updateLocallabGUI(pp, pedited, pp->locallab.selspot);
    updateSpecificGUIState();

    if (pp->locallab.nbspot > 0) {
        setParamEditable(true);

        // Locallab params are not editable if nbspot, selspot or id are not coherent (batch mode)
        if (pedited) {
            if (!pedited->locallab.nbspot || !pedited->locallab.selspot || !pedited->locallab.id) {
                setParamEditable(false);
            }
        }
    } else {
        setParamEditable(false);
    }

    // Enable all listeners
    enableListener();

    // Update default values according to selected spot
    if (pp->locallab.nbspot > 0 && pp->locallab.selspot < (int)pp->locallab.spots.size()) {
        setDefaults(defparams, defpedited, pp->locallab.spots.at(pp->locallab.selspot).id);
    }
}

void Locallab::write(rtengine::procparams::ProcParams* pp, ParamsEdited* pedited)
{
    // printf("Locallab write\n");

    pp->locallab.enabled = getEnabled();

    const int spotPanelEvent = expsettings->getEventType();
    int spotId;
    ControlSpotPanel::SpotRow* r;
    rtengine::procparams::LocallabParams::LocallabSpot* newSpot;

    int imW, imH; // Size of image
    int prW, prH; // Size of preview area
    int prX, prY; // Coord of preview area center
    EditDataProvider* const provider = expsettings->getEditProvider();

    switch (spotPanelEvent) {
        case (ControlSpotPanel::SpotCreation): // Spot creation event
            // Spot creation (default initialization)
            newSpot = new rtengine::procparams::LocallabParams::LocallabSpot();
            spotId = expsettings->getNewId();
            r = new ControlSpotPanel::SpotRow();
            r->id = newSpot->id = spotId;
            r->name = newSpot->name = M("TP_LOCALLAB_SPOTNAME") + std::to_string(spotId);
            r->isvisible = newSpot->isvisible;

            if (newSpot->shape == "ELI") {
                r->shape = 0;
            } else {
                r->shape = 1;
            }

            if (newSpot->spotMethod == "norm") {
                r->spotMethod = 0;
            } else {
                r->spotMethod = 1;
            }

            /*
                        if (newSpot->mergeMethod == "none") {
                            r->mergeMethod = 0;
                            expmaskcol1->hide();
                            expmaskcol->show();

                        } else if (newSpot->mergeMethod == "short") {
                            r->mergeMethod = 1;
                            expmaskcol1->hide();
                            expmaskcol->show();

                        } else if (newSpot->mergeMethod == "orig") {
                            r->mergeMethod = 2;
                            expmaskcol1->show();
                            expmaskcol->hide();
                        } else if (newSpot->mergeMethod == "origmas") {
                            r->mergeMethod = 3;
                            expmaskcol1->show();
                            expmaskcol->show();
                        } else if (newSpot->mergeMethod == "lastspot") {
                            r->mergeMethod = 4;
                            expmaskcol1->show();
                            expmaskcol->hide();
                        } else if (newSpot->mergeMethod == "lastspotmas") {
                            r->mergeMethod = 5;
                            expmaskcol1->show();
                            expmaskcol->show();

                        }
            */
            r->sensiexclu = newSpot->sensiexclu;
            r->structexclu = newSpot->structexclu;
            r->struc = newSpot->struc;

            if (newSpot->shapeMethod == "IND") {
                r->shapeMethod = 0;
            } else if (newSpot->shapeMethod == "SYM") {
                r->shapeMethod = 1;
            } else if (newSpot->shapeMethod == "INDSL") {
                r->shapeMethod = 2;
            } else {
                r->shapeMethod = 3;
            }

            // Calculate spot size and center position according to preview area
            if (provider && !batchMode) {
                provider->getImageSize(imW, imH);
                provider->getPreviewCenterPos(prX, prY);
                provider->getPreviewSize(prW, prH);

                if (imW && imH) { // Image loaded
                    // Spot center position computation
                    newSpot->centerX = rtengine::LIM(int(int((double)prX - (double)imW / 2.) * 2000. / (double)imW), -1000, 1000);
                    newSpot->centerY = rtengine::LIM(int(int((double)prY - (double)imH / 2.) * 2000. / (double)imH), -1000, 1000);
                    // Ellipse/rectangle size computation
                    newSpot->locX = rtengine::LIM(int(((double)prW / 2. - 5.) * 2000. / (double)imW), 2, newSpot->locX);
                    newSpot->locXL = rtengine::LIM(int(((double)prW / 2. - 5.) * 2000. / (double)imW), 2, newSpot->locXL);
                    newSpot->locY = rtengine::LIM(int(((double)prH / 2. - 5.) * 2000. / (double)imH), 2, newSpot->locY);
                    newSpot->locYT = rtengine::LIM(int(((double)prH / 2. - 5.) * 2000. / (double)imH), 2, newSpot->locYT);
                }
            }

            r->locX = newSpot->locX;
            r->locXL = newSpot->locXL;
            r->locY = newSpot->locY;
            r->locYT = newSpot->locYT;
            r->centerX = newSpot->centerX;
            r->centerY = newSpot->centerY;

            r->circrad = newSpot->circrad;

            if (newSpot->qualityMethod == "enh") {
                r->qualityMethod = 0;
            } else {
                r->qualityMethod = 1;
            }

            if (newSpot->complexMethod == "sim") {
                r->complexMethod = 0;
            } else  if (newSpot->complexMethod == "mod") {
                r->complexMethod = 1;
            } else  if (newSpot->complexMethod == "all") {
                r->complexMethod = 2;
            }

            r->transit = newSpot->transit;
            r->feather = newSpot->feather;
            r->thresh = newSpot->thresh;
            r->iter = newSpot->iter;
            r->balan = newSpot->balan;
            r->transitweak = newSpot->transitweak;
            r->transitgrad = newSpot->transitgrad;
            r->scopemask = newSpot->scopemask;
            r->lumask = newSpot->lumask;
            r->avoid = newSpot->avoid;
            r->recurs = newSpot->recurs;
            r->laplac = newSpot->laplac;
            r->deltae = newSpot->deltae;
            r->shortc = newSpot->shortc;
            r->savrest = newSpot->savrest;
            expsettings->addControlSpot(r);

            // ProcParams update
            pp->locallab.nbspot++;
            pp->locallab.selspot = pp->locallab.nbspot - 1;
            pp->locallab.spots.push_back(*newSpot);

            // New created spot selection
            expsettings->setSelectedSpot(spotId);

            // Update Locallab tools GUI with new created spot
            disableListener();

            if (pe) {
                pe->locallab.spots.push_back(new LocallabParamsEdited::LocallabSpotEdited(true));
            }

            updateLocallabGUI(pp, pe, pp->locallab.selspot);

            enableListener();

            if (pp->locallab.nbspot == 1) {
                setParamEditable(true);
            }

            // Update default values according to selected spot
            setDefaults(defparams, defpedited, spotId);

            // ParamsEdited update
            if (pedited) {
                pedited->locallab.nbspot = true;
                pedited->locallab.selspot = true;
                pedited->locallab.id = true;
                pedited->locallab.spots.push_back(new LocallabParamsEdited::LocallabSpotEdited(true));
            }

            break;

        case (ControlSpotPanel::SpotDeletion): // Spot deletion event
            // Get deleted spot index in ProcParams and update it
            spotId = expsettings->getSelectedSpot();

            for (int i = 0; i < pp->locallab.nbspot && i < (int)pp->locallab.spots.size(); i++) {
                if (pp->locallab.spots.at(i).id == spotId) {
                    // ProcParams update
                    pp->locallab.nbspot--;
                    pp->locallab.spots.erase(pp->locallab.spots.begin() + i);
                    expsettings->deleteControlSpot(spotId);

                    // Select the first remaining spot before deleted one
                    if (pp->locallab.nbspot > 0) {
                        for (int j = i - 1; j >= 0; j--) { // procparams spots uses zero-based index whereas spot ids use one-based index
                            if (expsettings->setSelectedSpot(j + 1)) { // True if an existing spot has been selected on controlspotpanel
                                pp->locallab.selspot = j;

                                break;
                            }
                        }
                    } else {
                        // Reset selspot
                        pp->locallab.selspot = 0;
                    }

                    // Update Locallab tools GUI with selected spot
                    disableListener();

                    if (pe) {
                        if (i < (int)pe->locallab.spots.size()) {
                            pe->locallab.spots.erase(pe->locallab.spots.begin() + i);
                        }
                    }

                    updateLocallabGUI(pp, pe, pp->locallab.selspot);

                    enableListener();

                    if (pp->locallab.nbspot == 0) {
                        setParamEditable(false);
                    }

                    // Update default values according to selected spot
                    if (pp->locallab.nbspot > 0) {
                        setDefaults(defparams, defpedited, pp->locallab.spots.at(pp->locallab.selspot).id);
                    }

                    // ParamsEdited update
                    if (pedited) {
                        pedited->locallab.nbspot = true;
                        pedited->locallab.selspot = true;
                        pedited->locallab.id = true;

                        if (i < (int)pedited->locallab.spots.size()) {
                            pedited->locallab.spots.erase(pedited->locallab.spots.begin() + i);
                        }
                    }

                    break;
                }
            }

            break;

        case (ControlSpotPanel::SpotSelection):  // Spot selection event
            spotId = expsettings->getSelectedSpot();

            for (int i = 0; i < pp->locallab.nbspot && i < (int)pp->locallab.spots.size(); i++) {
                if (pp->locallab.spots.at(i).id == spotId) {
                    pp->locallab.selspot = i;
                    break;
                }
            }

            // Update control spots and Locallab tools GUI with selected spot
            expsettings->setSelectedSpot(spotId);
            disableListener();
            updateLocallabGUI(pp, pe, pp->locallab.selspot);
            enableListener();

            // Update default values according to selected spot
            setDefaults(defparams, defpedited, spotId);

            // ParamsEdited update
            if (pedited) {
                pedited->locallab.selspot = true;
            }

            break;

        case (ControlSpotPanel::SpotDuplication): // Spot duplication event
            newSpot = nullptr;
            spotId = expsettings->getSelectedSpot();

            for (int i = 0; i < pp->locallab.nbspot && i < (int)pp->locallab.spots.size(); i++) {
                if (pp->locallab.spots.at(i).id == spotId) {
                    newSpot = new rtengine::procparams::LocallabParams::LocallabSpot(pp->locallab.spots.at(i));
                    break;
                }
            }

            if (!newSpot) {
                break;
            }

            // Spot creation (initialization at currently selected spot)
            spotId = expsettings->getNewId();
            r = new ControlSpotPanel::SpotRow();
            r->id = newSpot->id = spotId;
            r->name = newSpot->name = newSpot->name + " - " + M("TP_LOCALLAB_DUPLSPOTNAME");
            r->isvisible = newSpot->isvisible;

            if (newSpot->shape == "ELI") {
                r->shape = 0;
            } else {
                r->shape = 1;
            }

            if (newSpot->spotMethod == "norm") {
                r->spotMethod = 0;
            } else {
                r->spotMethod = 1;
            }

            /*
                        if (newSpot->mergeMethod == "none") {
                            r->mergeMethod = 0;
                            expmaskcol1->hide();
                            expmaskcol->show();
                        } else if (newSpot->mergeMethod == "short") {
                            r->mergeMethod = 1;
                            expmaskcol1->hide();
                            expmaskcol->show();
                        } else if (newSpot->mergeMethod == "orig") {
                            r->mergeMethod = 2;
                            expmaskcol1->show();
                            expmaskcol->hide();
                        } else if (newSpot->mergeMethod == "origmas") {
                            r->mergeMethod = 3;
                            expmaskcol1->show();
                            expmaskcol->show();
                        } else if (newSpot->mergeMethod == "lastspot") {
                            r->mergeMethod = 4;
                            expmaskcol1->show();
                            expmaskcol->hide();
                        } else if (newSpot->mergeMethod == "lastspotmas") {
                            r->mergeMethod = 5;
                            expmaskcol1->show();
                            expmaskcol->show();
                        }
            */
            r->sensiexclu = newSpot->sensiexclu;
            r->structexclu = newSpot->structexclu;
            r->struc = newSpot->struc;

            if (newSpot->shapeMethod == "IND") {
                r->shapeMethod = 0;
            } else if (newSpot->shapeMethod == "SYM") {
                r->shapeMethod = 1;
            } else if (newSpot->shapeMethod == "INDSL") {
                r->shapeMethod = 2;
            } else {
                r->shapeMethod = 3;
            }

            // Calculate spot size and center position according to preview area
            if (provider && !batchMode) {
                provider->getImageSize(imW, imH);
                provider->getPreviewCenterPos(prX, prY);
                provider->getPreviewSize(prW, prH);

                if (imW && imH) { // Image loaded
                    // Spot center position computation
                    newSpot->centerX = rtengine::LIM(int(int((double)prX - (double)imW / 2.) * 2000. / (double)imW), -1000, 1000);
                    newSpot->centerY = rtengine::LIM(int(int((double)prY - (double)imH / 2.) * 2000. / (double)imH), -1000, 1000);
                    // Ellipse/rectangle size computation
                    newSpot->locX = rtengine::LIM(int(((double)prW / 2. - 5.) * 2000. / (double)imW), 2, newSpot->locX);
                    newSpot->locXL = rtengine::LIM(int(((double)prW / 2. - 5.) * 2000. / (double)imW), 2, newSpot->locXL);
                    newSpot->locY = rtengine::LIM(int(((double)prH / 2. - 5.) * 2000. / (double)imH), 2, newSpot->locY);
                    newSpot->locYT = rtengine::LIM(int(((double)prH / 2. - 5.) * 2000. / (double)imH), 2, newSpot->locYT);
                }
            }

            r->locX = newSpot->locX;
            r->locXL = newSpot->locXL;
            r->locY = newSpot->locY;
            r->locYT = newSpot->locYT;
            r->centerX = newSpot->centerX;
            r->centerY = newSpot->centerY;

            r->circrad = newSpot->circrad;

            if (newSpot->qualityMethod == "enh") {
                r->qualityMethod = 0;
            } else {
                r->qualityMethod = 1;
            }

            if (newSpot->complexMethod == "sim") {
                r->complexMethod = 0;
            } else  if (newSpot->complexMethod == "mod") {
                r->complexMethod = 1;
            } else  if (newSpot->complexMethod == "all") {
                r->complexMethod = 2;
            }

            r->transit = newSpot->transit;
            r->feather = newSpot->feather;
            r->thresh = newSpot->thresh;
            r->iter = newSpot->iter;
            r->balan = newSpot->balan;
            r->transitweak = newSpot->transitweak;
            r->transitgrad = newSpot->transitgrad;
            r->scopemask = newSpot->scopemask;
            r->lumask = newSpot->lumask;
            r->avoid = newSpot->avoid;
            r->recurs = newSpot->recurs;
            r->laplac = newSpot->laplac;
            r->deltae = newSpot->deltae;
            r->shortc = newSpot->shortc;
            r->savrest = newSpot->savrest;
            expsettings->addControlSpot(r);

            // ProcParams update
            pp->locallab.nbspot++;
            pp->locallab.selspot = pp->locallab.nbspot - 1;
            pp->locallab.spots.push_back(*newSpot);

            // New created spot selection
            expsettings->setSelectedSpot(spotId);

            // Update Locallab tools GUI with new created spot
            disableListener();

            if (pe) {
                pe->locallab.spots.push_back(new LocallabParamsEdited::LocallabSpotEdited(true));
            }

            updateLocallabGUI(pp, pe, pp->locallab.selspot);

            enableListener();

            // Update default values according to selected spot
            setDefaults(defparams, defpedited, spotId);

            // ParamsEdited update
            if (pedited) {
                pedited->locallab.nbspot = true;
                pedited->locallab.selspot = true;
                pedited->locallab.id = true;
                pedited->locallab.spots.push_back(new LocallabParamsEdited::LocallabSpotEdited(true));
            }

            break;

        case (ControlSpotPanel::SpotAllVisibilityChanged): // Event when updating visibility of all spots
            r = expsettings->getSpot(expsettings->getSelectedSpot());

            // ProcParams update
            for (size_t i = 0; i < pp->locallab.spots.size(); i++) {
                pp->locallab.spots.at(i).isvisible = r->isvisible;
            }

            if (pe) {
                for (size_t i = 0; i < pe->locallab.spots.size(); i++) {
                    pe->locallab.spots.at(i).isvisible = true;
                }
            }

            // ParamsEdited update
            if (pedited) {
                for (size_t i = 0; i < pedited->locallab.spots.size(); i++) {
                    pedited->locallab.spots.at(i).isvisible = true;
                }
            }

            break;

        default: // Spot or locallab GUI updated
            if (pp->locallab.nbspot > 0) {
                r = expsettings->getSpot(expsettings->getSelectedSpot());

                // ProcParams update
                if (pp->locallab.selspot < (int)pp->locallab.spots.size()) {
                    // Control spot settings
                    pp->locallab.spots.at(pp->locallab.selspot).name = r->name;
                    pp->locallab.spots.at(pp->locallab.selspot).isvisible = r->isvisible;

                    if (r->shape == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).shape = "ELI";
                    } else {
                        pp->locallab.spots.at(pp->locallab.selspot).shape = "RECT";
                    }

                    if (r->spotMethod == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).spotMethod = "norm";
                    } else {
                        pp->locallab.spots.at(pp->locallab.selspot).spotMethod = "exc";
                    }

                    /*
                                        if (r->mergeMethod == 0) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "none";
                                            expmaskcol1->hide();
                                            expmaskcol->show();

                                        } else if (r->mergeMethod == 1) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "short";
                                            expmaskcol1->hide();
                                            expmaskcol->show();
                                        } else if (r->mergeMethod == 2) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "orig";
                                            expmaskcol1->show();
                                            expmaskcol->hide();
                                        } else if (r->mergeMethod == 3) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "origmas";
                                            expmaskcol1->show();
                                            expmaskcol->show();
                                        } else if (r->mergeMethod == 4) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "lastspot";
                                            expmaskcol1->show();
                                            expmaskcol->hide();
                                        } else if (r->mergeMethod == 5) {
                                            pp->locallab.spots.at(pp->locallab.selspot).mergeMethod = "lastspotmas";
                                            expmaskcol1->show();
                                            expmaskcol->show();

                                        }
                    */
                    pp->locallab.spots.at(pp->locallab.selspot).sensiexclu = r->sensiexclu;
                    pp->locallab.spots.at(pp->locallab.selspot).structexclu = r->structexclu;
                    pp->locallab.spots.at(pp->locallab.selspot).struc = r->struc;

                    if (r->shapeMethod == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).shapeMethod = "IND";
                    } else if (r->shapeMethod == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).shapeMethod = "SYM";
                    } else if (r->shapeMethod == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).shapeMethod = "INDSL";
                    } else {
                        pp->locallab.spots.at(pp->locallab.selspot).shapeMethod = "SYMSL";
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).locX = r->locX;
                    pp->locallab.spots.at(pp->locallab.selspot).locXL = r->locXL;
                    pp->locallab.spots.at(pp->locallab.selspot).locY = r->locY;
                    pp->locallab.spots.at(pp->locallab.selspot).locYT = r->locYT;
                    pp->locallab.spots.at(pp->locallab.selspot).centerX = r->centerX;
                    pp->locallab.spots.at(pp->locallab.selspot).centerY = r->centerY;
                    pp->locallab.spots.at(pp->locallab.selspot).circrad = r->circrad;

                    if (r->qualityMethod == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).qualityMethod = "enh";
                    } else {
                        pp->locallab.spots.at(pp->locallab.selspot).qualityMethod = "enhden";
                    }

                    if (r->complexMethod == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).complexMethod = "sim";
                    } else if (r->complexMethod == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).complexMethod = "mod";
                    } else if (r->complexMethod == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).complexMethod = "all";
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).transit = r->transit;
                    pp->locallab.spots.at(pp->locallab.selspot).feather = r->feather;
                    pp->locallab.spots.at(pp->locallab.selspot).thresh = r->thresh;
                    pp->locallab.spots.at(pp->locallab.selspot).iter = r->iter;
                    pp->locallab.spots.at(pp->locallab.selspot).balan = r->balan;
                    pp->locallab.spots.at(pp->locallab.selspot).transitweak = r->transitweak;
                    pp->locallab.spots.at(pp->locallab.selspot).transitgrad = r->transitgrad;
                    pp->locallab.spots.at(pp->locallab.selspot).scopemask = r->scopemask;
                    pp->locallab.spots.at(pp->locallab.selspot).lumask = r->lumask;
                    pp->locallab.spots.at(pp->locallab.selspot).avoid = r->avoid;
                    pp->locallab.spots.at(pp->locallab.selspot).recurs = r->recurs;
                    pp->locallab.spots.at(pp->locallab.selspot).laplac = r->laplac;
                    pp->locallab.spots.at(pp->locallab.selspot).deltae = r->deltae;
                    pp->locallab.spots.at(pp->locallab.selspot).shortc = r->shortc;
                    pp->locallab.spots.at(pp->locallab.selspot).savrest = r->savrest;
                    // Color & Light
                    pp->locallab.spots.at(pp->locallab.selspot).expcolor = expcolor->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).curvactiv = curvactiv->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).lightness = lightness->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).contrast = contrast->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chroma = chroma->getIntValue();
                    labgrid->getParams(pp->locallab.spots.at(pp->locallab.selspot).labgridALow, pp->locallab.spots.at(pp->locallab.selspot).labgridBLow, pp->locallab.spots.at(pp->locallab.selspot).labgridAHigh, pp->locallab.spots.at(pp->locallab.selspot).labgridBHigh);
                    labgridmerg->getParams(pp->locallab.spots.at(pp->locallab.selspot).labgridALowmerg, pp->locallab.spots.at(pp->locallab.selspot).labgridBLowmerg, pp->locallab.spots.at(pp->locallab.selspot).labgridAHighmerg, pp->locallab.spots.at(pp->locallab.selspot).labgridBHighmerg);
                    pp->locallab.spots.at(pp->locallab.selspot).strengthgrid = strengthgrid->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).labgridALow *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridAHigh *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridBLow *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridBHigh *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridALowmerg *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridAHighmerg *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridBLowmerg *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).labgridBHighmerg *= rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX;
                    pp->locallab.spots.at(pp->locallab.selspot).sensi = sensi->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).structcol = structcol->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).blurcolde = blurcolde->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).blurcol = blurcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).contcol = contcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strcol = strcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).angcol = angcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strcolab = strcolab->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strcolh = strcolh->getValue();

                    if (qualitycurveMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod = "none";
                    } else if (qualitycurveMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod = "std";
                    }

                    if (gridMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).gridMethod = "one";
                    } else if (gridMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).gridMethod = "two";
                    }

                    if (merMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).merMethod = "mone";
                    } else if (merMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).merMethod = "mtwo";
                    } else if (merMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).merMethod = "mthr";
                    } else if (merMethod->get_active_row_number() == 3) {
                        pp->locallab.spots.at(pp->locallab.selspot).merMethod = "mfou";
                    } else if (merMethod->get_active_row_number() == 4) {
                        pp->locallab.spots.at(pp->locallab.selspot).merMethod = "mfiv";
                    }

                    if (toneMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).toneMethod = "one";
                    } else if (toneMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).toneMethod = "two";
                    } else if (toneMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).toneMethod = "thr";
                    } else if (toneMethod->get_active_row_number() == 3) {
                        pp->locallab.spots.at(pp->locallab.selspot).toneMethod = "fou";
                    }

                    if (mergecolMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "one";
                    } else if (mergecolMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "two";
                    } else if (mergecolMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "thr";
                    } else if (mergecolMethod->get_active_row_number() == 3) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "fou";
                    } else if (mergecolMethod->get_active_row_number() == 4) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "fiv";
                    } else if (mergecolMethod->get_active_row_number() == 5) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "six";
                    } else if (mergecolMethod->get_active_row_number() == 6) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "sev";
                    } else if (mergecolMethod->get_active_row_number() == 7) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "sev0";
                    } else if (mergecolMethod->get_active_row_number() == 8) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "sev1";
                    } else if (mergecolMethod->get_active_row_number() == 9) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "sev2";
                    } else if (mergecolMethod->get_active_row_number() == 10) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "hei";
                    } else if (mergecolMethod->get_active_row_number() == 11) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "nin";
                    } else if (mergecolMethod->get_active_row_number() == 12) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "ten";
                    } else if (mergecolMethod->get_active_row_number() == 13) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "ele";
                    } else if (mergecolMethod->get_active_row_number() == 14) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "twe";
                    } else if (mergecolMethod->get_active_row_number() == 15) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "thi";
                    } else if (mergecolMethod->get_active_row_number() == 16) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "for";
                    } else if (mergecolMethod->get_active_row_number() == 17) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "hue";
                    } else if (mergecolMethod->get_active_row_number() == 18) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "sat";
                    } else if (mergecolMethod->get_active_row_number() == 19) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "col";
                    } else if (mergecolMethod->get_active_row_number() == 20) {
                        pp->locallab.spots.at(pp->locallab.selspot).mergecolMethod = "lum";
                    }

                    if (complexsoft == 2) {
                        lcshape->reset();
                        clshape->reset();
                        LHshape->reset();
                        HHshape->reset();
                        rgbshape->reset();
                        HHhmaskshape->reset();
                    }

                    if (complexsoft > 0) {
                        LLmaskcolshapewav->reset();
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).llcurve = llshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).cccurve = ccshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).clcurve = clshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).lccurve = lcshape->getCurve();

                    pp->locallab.spots.at(pp->locallab.selspot).rgbcurve = rgbshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).LHcurve = LHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHcurve = HHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).invers = invers->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).special = special->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).toolcol = toolcol->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).enaColorMask = enaColorMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).fftColorMask = fftColorMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskcurve = CCmaskshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskcurve = LLmaskshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskcurve = HHmaskshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHhmaskcurve = HHhmaskshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskcol = blendmaskcol->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskcol = radmaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskcol = chromaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskcol = gammaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskcol = slomaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shadmaskcol = shadmaskcol->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strumaskcol = strumaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskcol = lapmaskcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).softradiuscol = softradiuscol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).opacol = opacol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).mercol = mercol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).merlucol = merlucol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).conthrcol = conthrcol->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskcurve = Lmaskshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskcolcurvewav = LLmaskcolshapewav->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).csthresholdcol = csThresholdcol->getValue<int>();
                    // Exposure
                    pp->locallab.spots.at(pp->locallab.selspot).expexpose = expexpose->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).expcomp = expcomp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).hlcompr = hlcompr->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).hlcomprthresh = hlcomprthresh->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).black = black->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shadex = shadex->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shcompr = shcompr->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).expchroma = expchroma->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).warm = warm->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensiex = sensiex->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).structexp = structexp->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).blurexpde = blurexpde->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strexp = strexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).angexp = angexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).excurve = shapeexpos->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).inversex = inversex->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).enaExpMask = enaExpMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).enaExpMaskaft = enaExpMaskaft->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskexpcurve = LLmaskexpshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskexpcurve = CCmaskexpshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskexpcurve = HHmaskexpshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskexp = blendmaskexp->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskexp = radmaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskexp = chromaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskexp = gammaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskexp = slomaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskexp = lapmaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strmaskexp = strmaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).angmaskexp = angmaskexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).softradiusexp = softradiusexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskexpcurve = Lmaskexpshape->getCurve();

                    if (expMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).expMethod = "std";
                    } else if (expMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).expMethod = "pde";
                    }

                    if (exnoiseMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "none";
                    } else if (exnoiseMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "med";
                    } else if (exnoiseMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "medhi";
                    }

                    /*
                    } else if (exnoiseMethod->get_active_row_number() == 3) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "wavlo";
                    } else if (exnoiseMethod->get_active_row_number() == 4) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "wavme";
                    } else if (exnoiseMethod->get_active_row_number() == 5) {
                        pp->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = "wavhi";
                    }
                    */
                    pp->locallab.spots.at(pp->locallab.selspot).laplacexp = laplacexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).balanexp = balanexp->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).linear = linear->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gamm = gamm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fatamount = fatamount->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fatdetail = fatdetail->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fatanchor = fatanchor->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fatlevel = fatlevel->getIntValue();

                    // Shadow highlight
                    pp->locallab.spots.at(pp->locallab.selspot).expshadhigh = expshadhigh->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).highlights = highlights->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).h_tonalwidth = h_tonalwidth->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shadows = shadows->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).s_tonalwidth = s_tonalwidth->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sh_radius = sh_radius->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensihs = sensihs->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).enaSHMask = enaSHMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskSHcurve = LLmaskSHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskSHcurve = CCmaskSHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskSHcurve = HHmaskSHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskSH = blendmaskSH->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskSH = radmaskSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).blurSHde = blurSHde->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strSH = strSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).angSH = angSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).inverssh = inverssh->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskSH = chromaskSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskSH = gammaskSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskSH = slomaskSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskSH = lapmaskSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).detailSH = detailSH->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).LmaskSHcurve = LmaskSHshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).fatamountSH = fatamountSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fatanchorSH = fatanchorSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gamSH = gamSH->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sloSH = sloSH->getValue();

                    if (shMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).shMethod = "std";
                    } else if (shMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).shMethod = "tone";
                    }

                    for (int i = 0; i < 5; i++) {
                        pp->locallab.spots.at(pp->locallab.selspot).multsh[i] = multipliersh[i]->getIntValue();
                    }

                    // Vibrance
                    pp->locallab.spots.at(pp->locallab.selspot).expvibrance = expvibrance->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).saturated = saturated->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).pastels = pastels->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).psthreshold = psThreshold->getValue<int>();
                    pp->locallab.spots.at(pp->locallab.selspot).protectskins = protectSkins->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).avoidcolorshift = avoidColorShift->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).pastsattog = pastSatTog->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).sensiv = sensiv->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).skintonescurve = skinTonesCurve->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskvibcurve = LLmaskvibshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskvibcurve = CCmaskvibshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskvibcurve = HHmaskvibshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).enavibMask = enavibMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskvib = blendmaskvib->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskvib = radmaskvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskvib = chromaskvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskvib = gammaskvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskvib = slomaskvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskvib = lapmaskvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskvibcurve = Lmaskvibshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).strvib = strvib->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strvibab = strvibab->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strvibh = strvibh->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).angvib = angvib->getValue();

                    if (complexsoft == 2) {
                        skinTonesCurve->reset();
                    }

                    // Soft Light
                    pp->locallab.spots.at(pp->locallab.selspot).expsoft = expsoft->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).streng = streng->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensisf = sensisf->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).laplace = laplace->getValue();

                    if (softMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).softMethod = "soft";
                    } else if (softMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).softMethod = "reti";
                    }

                    // Blur & Noise
                    pp->locallab.spots.at(pp->locallab.selspot).expblur = expblur->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).radius = radius->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strength = strength->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensibn = sensibn->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).itera = itera->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).guidbl = guidbl->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).epsbl = epsbl->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).isogr = isogr->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strengr = strengr->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).scalegr = scalegr->getIntValue();

                    if (blMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).blMethod = "blur";
                    } else if (blMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).blMethod = "med";
                    } else if (blMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).blMethod = "guid";
                    }

                    if (medMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).medMethod = "none";
                    } else if (medMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).medMethod = "33";
                    } else if (medMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).medMethod = "55";
                    } else if (medMethod->get_active_row_number() == 3) {
                        pp->locallab.spots.at(pp->locallab.selspot).medMethod = "77";
                    } else if (medMethod->get_active_row_number() == 4) {
                        pp->locallab.spots.at(pp->locallab.selspot).medMethod = "99";
                    }


                    if (blurMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).blurMethod = "norm";
                    } else if (blurMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).blurMethod = "inv";
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).activlum = activlum->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskblcurve = LLmaskblshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskblcurve = CCmaskblshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskblcurve = HHmaskblshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).enablMask = enablMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskbl = blendmaskbl->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskbl = radmaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskbl = chromaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskbl = gammaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskbl = slomaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskbl = lapmaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shadmaskbl = shadmaskbl->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).strumaskbl = strumaskbl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fftwbl = fftwbl->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).toolbl = toolbl->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskblcurve = Lmaskblshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskblcurvewav = LLmaskblshapewav->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).csthresholdblur = csThresholdblur->getValue<int>();

                    // Tone Mapping
                    pp->locallab.spots.at(pp->locallab.selspot).exptonemap = exptonemap->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).stren = stren->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gamma = gamma->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).estop = estop->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).scaltm = scaltm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).rewei = rewei->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).satur = satur->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensitm = sensitm->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).softradiustm = softradiustm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).amount = amount->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).equiltm = equiltm->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmasktmcurve = LLmasktmshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmasktmcurve = CCmasktmshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmasktmcurve = HHmasktmshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).enatmMask = enatmMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).enatmMaskaft = enatmMaskaft->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmasktm = blendmasktm->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmasktm = radmasktm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromasktm = chromasktm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammasktm = gammasktm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomasktm = slomasktm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmasktm = lapmasktm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmasktmcurve = Lmasktmshape->getCurve();
                    // Retinex
                    pp->locallab.spots.at(pp->locallab.selspot).expreti = expreti->getEnabled();

                    if (retinexMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).retinexMethod = "low";
                    } else if (retinexMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).retinexMethod = "uni";
                    } else if (retinexMethod->get_active_row_number() == 2) {
                        pp->locallab.spots.at(pp->locallab.selspot).retinexMethod = "high";
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).str = str->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chrrt = chrrt->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).neigh = neigh->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).vart = vart->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).offs = offs->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).dehaz = dehaz->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).depth = depth->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensih = sensih->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).localTgaincurve = cTgainshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).localTtranscurve = cTtransshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).inversret = inversret->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).softradiusret = softradiusret->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).equilret = equilret->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).loglin = loglin->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).lumonly = lumonly->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskreticurve = LLmaskretishape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskreticurve = CCmaskretishape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskreticurve = HHmaskretishape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).enaretiMask = enaretiMask->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap = enaretiMasktmap->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskreti = blendmaskreti->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskreti = radmaskreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskreti = chromaskreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskreti = gammaskreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskreti = slomaskreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskreti = lapmaskreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).scalereti = scalereti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).darkness = darkness->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lightnessreti = lightnessreti->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).limd = limd->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).cliptm = cliptm->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fftwreti = fftwreti->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskreticurve = Lmaskretishape->getCurve();
                    // Sharpening
                    pp->locallab.spots.at(pp->locallab.selspot).expsharp = expsharp->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).sharcontrast = sharcontrast->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sharradius = sharradius->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sharamount = sharamount->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shardamping = shardamping->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).shariter = shariter->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sharblur = sharblur->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensisha = sensisha->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).inverssha = inverssha->get_active();
                    // Local Contrast
                    pp->locallab.spots.at(pp->locallab.selspot).expcontrast = expcontrast->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).lcradius = lcradius->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lcamount = lcamount->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lcdarkness = lcdarkness->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lclightness = lclightness->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).levelwav = levelwav->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).residcont = residcont->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).residchro = residchro->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).clarilres = clarilres->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).claricres = claricres->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).clarisoft = clarisoft->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensilc = sensilc->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).fftwlc = fftwlc->get_active();
                    pp->locallab.spots.at(pp->locallab.selspot).locwavcurve = wavshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).csthreshold = csThreshold->getValue<int>();

                    if (localcontMethod->get_active_row_number() == 0) {
                        pp->locallab.spots.at(pp->locallab.selspot).localcontMethod = "loc";
                    } else if (localcontMethod->get_active_row_number() == 1) {
                        pp->locallab.spots.at(pp->locallab.selspot).localcontMethod = "wav";
                    }

                    // Contrast by detail levels
                    pp->locallab.spots.at(pp->locallab.selspot).expcbdl = expcbdl->getEnabled();

                    for (int i = 0; i < 6; i++) {
                        pp->locallab.spots.at(pp->locallab.selspot).mult[i] = multiplier[i]->getValue();
                    }

                    pp->locallab.spots.at(pp->locallab.selspot).chromacbdl = chromacbdl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).threshold = threshold->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensicb = sensicb->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).clarityml = clarityml->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).contresid = contresid->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).blurcbdl = blurcbdl->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).softradiuscb = softradiuscb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).enacbMask = enacbMask->get_active();

                    pp->locallab.spots.at(pp->locallab.selspot).LLmaskcbcurve = LLmaskcbshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).CCmaskcbcurve = CCmaskcbshape->getCurve();
                    pp->locallab.spots.at(pp->locallab.selspot).HHmaskcbcurve = HHmaskcbshape->getCurve();

                    pp->locallab.spots.at(pp->locallab.selspot).blendmaskcb = blendmaskcb->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).radmaskcb = radmaskcb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).chromaskcb = chromaskcb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).gammaskcb = gammaskcb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).slomaskcb = slomaskcb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).lapmaskcb = lapmaskcb->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).Lmaskcbcurve = Lmaskcbshape->getCurve();

                    // Denoise
                    pp->locallab.spots.at(pp->locallab.selspot).expdenoi = expdenoi->getEnabled();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselumf = noiselumf->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselumf0 = noiselumf0->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselumf2 = noiselumf2->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselumc = noiselumc->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselumdetail = noiselumdetail->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noiselequal = noiselequal->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noisechrof = noisechrof->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noisechroc = noisechroc->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).noisechrodetail = noisechrodetail->getValue();
                    pp->locallab.spots.at(pp->locallab.selspot).adjblur = adjblur->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).bilateral = bilateral->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).sensiden = sensiden->getIntValue();
                    pp->locallab.spots.at(pp->locallab.selspot).detailthr = detailthr->getIntValue();
                }

                ControlSpotPanel::SpotEdited* const se = expsettings->getEditedStates();

                if (pe) {
                    if (pp->locallab.selspot < (int)pe->locallab.spots.size()) {
                        pe->locallab.spots.at(pp->locallab.selspot).name = pe->locallab.spots.at(pp->locallab.selspot).name || se->name;
                        pe->locallab.spots.at(pp->locallab.selspot).isvisible = pe->locallab.spots.at(pp->locallab.selspot).isvisible || se->isvisible;
                        pe->locallab.spots.at(pp->locallab.selspot).shape = pe->locallab.spots.at(pp->locallab.selspot).shape || se->shape;
                        pe->locallab.spots.at(pp->locallab.selspot).spotMethod = pe->locallab.spots.at(pp->locallab.selspot).spotMethod || se->spotMethod;
//                        pe->locallab.spots.at(pp->locallab.selspot).mergeMethod = pe->locallab.spots.at(pp->locallab.selspot).mergeMethod || se->mergeMethod;
                        pe->locallab.spots.at(pp->locallab.selspot).sensiexclu = pe->locallab.spots.at(pp->locallab.selspot).sensiexclu || se->sensiexclu;
                        pe->locallab.spots.at(pp->locallab.selspot).structexclu = pe->locallab.spots.at(pp->locallab.selspot).structexclu || se->structexclu;
                        pe->locallab.spots.at(pp->locallab.selspot).struc = pe->locallab.spots.at(pp->locallab.selspot).struc || se->struc;
                        pe->locallab.spots.at(pp->locallab.selspot).shapeMethod = pe->locallab.spots.at(pp->locallab.selspot).shapeMethod || se->shapeMethod;
                        pe->locallab.spots.at(pp->locallab.selspot).locX = pe->locallab.spots.at(pp->locallab.selspot).locX || se->locX;
                        pe->locallab.spots.at(pp->locallab.selspot).locXL = pe->locallab.spots.at(pp->locallab.selspot).locXL || se->locXL;
                        pe->locallab.spots.at(pp->locallab.selspot).locY = pe->locallab.spots.at(pp->locallab.selspot).locY || se->locY;
                        pe->locallab.spots.at(pp->locallab.selspot).locYT = pe->locallab.spots.at(pp->locallab.selspot).locYT || se->locYT;
                        pe->locallab.spots.at(pp->locallab.selspot).centerX = pe->locallab.spots.at(pp->locallab.selspot).centerX || se->centerX;
                        pe->locallab.spots.at(pp->locallab.selspot).centerY = pe->locallab.spots.at(pp->locallab.selspot).centerY || se->centerY;
                        pe->locallab.spots.at(pp->locallab.selspot).circrad = pe->locallab.spots.at(pp->locallab.selspot).circrad || se->circrad;
                        pe->locallab.spots.at(pp->locallab.selspot).qualityMethod = pe->locallab.spots.at(pp->locallab.selspot).qualityMethod || se->qualityMethod;
                        pe->locallab.spots.at(pp->locallab.selspot).complexMethod = pe->locallab.spots.at(pp->locallab.selspot).complexMethod || se->complexMethod;
                        pe->locallab.spots.at(pp->locallab.selspot).transit = pe->locallab.spots.at(pp->locallab.selspot).transit || se->transit;
                        pe->locallab.spots.at(pp->locallab.selspot).feather = pe->locallab.spots.at(pp->locallab.selspot).feather || se->feather;
                        pe->locallab.spots.at(pp->locallab.selspot).thresh = pe->locallab.spots.at(pp->locallab.selspot).thresh || se->thresh;
                        pe->locallab.spots.at(pp->locallab.selspot).iter = pe->locallab.spots.at(pp->locallab.selspot).iter || se->iter;
                        pe->locallab.spots.at(pp->locallab.selspot).transitweak = pe->locallab.spots.at(pp->locallab.selspot).transitweak || se->transitweak;
                        pe->locallab.spots.at(pp->locallab.selspot).transitgrad = pe->locallab.spots.at(pp->locallab.selspot).transitgrad || se->transitgrad;
                        pe->locallab.spots.at(pp->locallab.selspot).scopemask = pe->locallab.spots.at(pp->locallab.selspot).scopemask || se->scopemask;
                        pe->locallab.spots.at(pp->locallab.selspot).lumask = pe->locallab.spots.at(pp->locallab.selspot).lumask || se->lumask;
                        pe->locallab.spots.at(pp->locallab.selspot).balan = pe->locallab.spots.at(pp->locallab.selspot).balan || se->balan;
                        pe->locallab.spots.at(pp->locallab.selspot).avoid = pe->locallab.spots.at(pp->locallab.selspot).avoid || se->avoid;
                        pe->locallab.spots.at(pp->locallab.selspot).recurs = pe->locallab.spots.at(pp->locallab.selspot).recurs || se->recurs;
                        pe->locallab.spots.at(pp->locallab.selspot).laplac = pe->locallab.spots.at(pp->locallab.selspot).laplac || se->laplac;
                        pe->locallab.spots.at(pp->locallab.selspot).deltae = pe->locallab.spots.at(pp->locallab.selspot).deltae || se->deltae;
                        pe->locallab.spots.at(pp->locallab.selspot).shortc = pe->locallab.spots.at(pp->locallab.selspot).shortc || se->shortc;
                        pe->locallab.spots.at(pp->locallab.selspot).savrest = pe->locallab.spots.at(pp->locallab.selspot).savrest || se->savrest;
                        // Color & Light
                        pe->locallab.spots.at(pp->locallab.selspot).expcolor = pe->locallab.spots.at(pp->locallab.selspot).expcolor || !expcolor->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).curvactiv = pe->locallab.spots.at(pp->locallab.selspot).curvactiv || !curvactiv->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).lightness = pe->locallab.spots.at(pp->locallab.selspot).lightness || lightness->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).contrast = pe->locallab.spots.at(pp->locallab.selspot).contrast || contrast->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).labgridALow = pe->locallab.spots.at(pp->locallab.selspot).labgridBLow = pe->locallab.spots.at(pp->locallab.selspot).labgridAHigh = pe->locallab.spots.at(pp->locallab.selspot).labgridBHigh = labgrid->getEdited();
                        pe->locallab.spots.at(pp->locallab.selspot).labgridALowmerg = pe->locallab.spots.at(pp->locallab.selspot).labgridBLowmerg = pe->locallab.spots.at(pp->locallab.selspot).labgridAHighmerg = pe->locallab.spots.at(pp->locallab.selspot).labgridBHighmerg = labgridmerg->getEdited();
                        pe->locallab.spots.at(pp->locallab.selspot).strengthgrid = pe->locallab.spots.at(pp->locallab.selspot).strengthgrid || strengthgrid->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chroma = pe->locallab.spots.at(pp->locallab.selspot).chroma || chroma->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensi = pe->locallab.spots.at(pp->locallab.selspot).sensi || sensi->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).structcol = pe->locallab.spots.at(pp->locallab.selspot).structcol || structcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strcol = pe->locallab.spots.at(pp->locallab.selspot).strcol || strcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strcolab = pe->locallab.spots.at(pp->locallab.selspot).strcolab || strcolab->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strcolh = pe->locallab.spots.at(pp->locallab.selspot).strcolh || strcolh->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).angcol = pe->locallab.spots.at(pp->locallab.selspot).angcol || angcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod = pe->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod || qualitycurveMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).gridMethod = pe->locallab.spots.at(pp->locallab.selspot).gridMethod || gridMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).merMethod = pe->locallab.spots.at(pp->locallab.selspot).merMethod || merMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).toneMethod = pe->locallab.spots.at(pp->locallab.selspot).toneMethod || toneMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).mergecolMethod = pe->locallab.spots.at(pp->locallab.selspot).mergecolMethod || mergecolMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).llcurve = pe->locallab.spots.at(pp->locallab.selspot).llcurve || !llshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).lccurve = pe->locallab.spots.at(pp->locallab.selspot).lccurve || !lcshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).rgbcurve = pe->locallab.spots.at(pp->locallab.selspot).rgbcurve || !rgbshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).cccurve = pe->locallab.spots.at(pp->locallab.selspot).cccurve || !ccshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).clcurve = pe->locallab.spots.at(pp->locallab.selspot).clcurve || !clshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).lccurve = pe->locallab.spots.at(pp->locallab.selspot).lccurve || !lcshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LHcurve = pe->locallab.spots.at(pp->locallab.selspot).LHcurve || !LHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHcurve = pe->locallab.spots.at(pp->locallab.selspot).HHcurve || !HHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).invers = pe->locallab.spots.at(pp->locallab.selspot).invers || !invers->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).special = pe->locallab.spots.at(pp->locallab.selspot).special || !special->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).toolcol = pe->locallab.spots.at(pp->locallab.selspot).toolcol || !toolcol->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).enaColorMask = pe->locallab.spots.at(pp->locallab.selspot).enaColorMask || !enaColorMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).fftColorMask = pe->locallab.spots.at(pp->locallab.selspot).fftColorMask || !fftColorMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskcurve || !CCmaskshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskcurve || !LLmaskshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskcurve || !HHmaskshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHhmaskcurve = pe->locallab.spots.at(pp->locallab.selspot).HHhmaskcurve || !HHhmaskshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).blurcolde = pe->locallab.spots.at(pp->locallab.selspot).blurcolde || blurcolde->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blurcol = pe->locallab.spots.at(pp->locallab.selspot).blurcol || blurcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).contcol = pe->locallab.spots.at(pp->locallab.selspot).contcol || contcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskcol = pe->locallab.spots.at(pp->locallab.selspot).blendmaskcol || blendmaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskcol = pe->locallab.spots.at(pp->locallab.selspot).radmaskcol || radmaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskcol = pe->locallab.spots.at(pp->locallab.selspot).chromaskcol || chromaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskcol = pe->locallab.spots.at(pp->locallab.selspot).gammaskcol || gammaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskcol = pe->locallab.spots.at(pp->locallab.selspot).slomaskcol || slomaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shadmaskcol = pe->locallab.spots.at(pp->locallab.selspot).shadmaskcol || shadmaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strumaskcol = pe->locallab.spots.at(pp->locallab.selspot).strumaskcol || strumaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskcol = pe->locallab.spots.at(pp->locallab.selspot).lapmaskcol || lapmaskcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).softradiuscol = pe->locallab.spots.at(pp->locallab.selspot).softradiuscol || softradiuscol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).opacol = pe->locallab.spots.at(pp->locallab.selspot).opacol || opacol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).mercol = pe->locallab.spots.at(pp->locallab.selspot).mercol || mercol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).merlucol = pe->locallab.spots.at(pp->locallab.selspot).merlucol || merlucol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).conthrcol = pe->locallab.spots.at(pp->locallab.selspot).conthrcol || conthrcol->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskcurve || !Lmaskshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskcolcurvewav = pe->locallab.spots.at(pp->locallab.selspot).LLmaskcolcurvewav || !LLmaskcolshapewav->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).csthresholdcol = pe->locallab.spots.at(pp->locallab.selspot).csthresholdcol || csThresholdcol->getEditedState();
                        // Exposure
                        pe->locallab.spots.at(pp->locallab.selspot).expexpose = pe->locallab.spots.at(pp->locallab.selspot).expexpose || !expexpose->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).expcomp = pe->locallab.spots.at(pp->locallab.selspot).expcomp || expcomp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).hlcompr = pe->locallab.spots.at(pp->locallab.selspot).hlcompr || hlcompr->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).hlcomprthresh = pe->locallab.spots.at(pp->locallab.selspot).hlcomprthresh || hlcomprthresh->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).black = pe->locallab.spots.at(pp->locallab.selspot).black || black->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shadex = pe->locallab.spots.at(pp->locallab.selspot).shadex || shadex->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shcompr = pe->locallab.spots.at(pp->locallab.selspot).shcompr || shcompr->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).expchroma = pe->locallab.spots.at(pp->locallab.selspot).expchroma || expchroma->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).warm = pe->locallab.spots.at(pp->locallab.selspot).warm || warm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensiex = pe->locallab.spots.at(pp->locallab.selspot).sensiex || sensiex->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).structexp = pe->locallab.spots.at(pp->locallab.selspot).structexp || structexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blurexpde = pe->locallab.spots.at(pp->locallab.selspot).blurexpde || blurexpde->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strexp = pe->locallab.spots.at(pp->locallab.selspot).strexp || strexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).angexp = pe->locallab.spots.at(pp->locallab.selspot).angexp || angexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).excurve = pe->locallab.spots.at(pp->locallab.selspot).excurve || !shapeexpos->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).inversex = pe->locallab.spots.at(pp->locallab.selspot).inversex || !inversex->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).enaExpMask = pe->locallab.spots.at(pp->locallab.selspot).enaExpMask || !enaExpMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).enaExpMaskaft = pe->locallab.spots.at(pp->locallab.selspot).enaExpMaskaft || !enaExpMaskaft->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskexpcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskexpcurve || !CCmaskexpshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskexpcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskexpcurve || !LLmaskexpshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskexpcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskexpcurve || !HHmaskexpshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskexp = pe->locallab.spots.at(pp->locallab.selspot).blendmaskexp || blendmaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskexp = pe->locallab.spots.at(pp->locallab.selspot).radmaskexp || radmaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskexp = pe->locallab.spots.at(pp->locallab.selspot).chromaskexp || chromaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskexp = pe->locallab.spots.at(pp->locallab.selspot).gammaskexp || gammaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskexp = pe->locallab.spots.at(pp->locallab.selspot).slomaskexp || slomaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskexp = pe->locallab.spots.at(pp->locallab.selspot).lapmaskexp || lapmaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strmaskexp = pe->locallab.spots.at(pp->locallab.selspot).strmaskexp || strmaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).angmaskexp = pe->locallab.spots.at(pp->locallab.selspot).angmaskexp || angmaskexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).softradiusexp = pe->locallab.spots.at(pp->locallab.selspot).softradiusexp || softradiusexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskexpcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskexpcurve || !Lmaskexpshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).expMethod = pe->locallab.spots.at(pp->locallab.selspot).expMethod || expMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = pe->locallab.spots.at(pp->locallab.selspot).exnoiseMethod || exnoiseMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).laplacexp = pe->locallab.spots.at(pp->locallab.selspot).laplacexp || laplacexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).balanexp = pe->locallab.spots.at(pp->locallab.selspot).balanexp || balanexp->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).linear = pe->locallab.spots.at(pp->locallab.selspot).linear || linear->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gamm = pe->locallab.spots.at(pp->locallab.selspot).gamm || gamm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fatamount = pe->locallab.spots.at(pp->locallab.selspot).fatamount || fatamount->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fatdetail = pe->locallab.spots.at(pp->locallab.selspot).fatdetail || fatdetail->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fatanchor = pe->locallab.spots.at(pp->locallab.selspot).fatanchor || fatanchor->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fatlevel = pe->locallab.spots.at(pp->locallab.selspot).fatlevel || fatlevel->getEditedState();
                        // Shadow highlight
                        pe->locallab.spots.at(pp->locallab.selspot).expshadhigh = pe->locallab.spots.at(pp->locallab.selspot).expshadhigh || !expshadhigh->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).highlights = pe->locallab.spots.at(pp->locallab.selspot).highlights || highlights->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).h_tonalwidth = pe->locallab.spots.at(pp->locallab.selspot).h_tonalwidth || h_tonalwidth->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shadows = pe->locallab.spots.at(pp->locallab.selspot).shadows || shadows->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).s_tonalwidth = pe->locallab.spots.at(pp->locallab.selspot).s_tonalwidth || s_tonalwidth->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sh_radius = pe->locallab.spots.at(pp->locallab.selspot).sh_radius || sh_radius->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensihs = pe->locallab.spots.at(pp->locallab.selspot).sensihs || sensihs->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).enaSHMask = pe->locallab.spots.at(pp->locallab.selspot).enaSHMask || !enaSHMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskSHcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskSHcurve || !CCmaskSHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskSHcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskSHcurve || !LLmaskSHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskSHcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskSHcurve || !HHmaskSHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskSH = pe->locallab.spots.at(pp->locallab.selspot).blendmaskSH || blendmaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskSH = pe->locallab.spots.at(pp->locallab.selspot).radmaskSH || radmaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blurSHde = pe->locallab.spots.at(pp->locallab.selspot).blurSHde || blurSHde->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strSH = pe->locallab.spots.at(pp->locallab.selspot).strSH || strSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).angSH = pe->locallab.spots.at(pp->locallab.selspot).angSH || angSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).inverssh = pe->locallab.spots.at(pp->locallab.selspot).inverssh || !inverssh->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskSH = pe->locallab.spots.at(pp->locallab.selspot).chromaskSH || chromaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskSH = pe->locallab.spots.at(pp->locallab.selspot).gammaskSH || gammaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskSH = pe->locallab.spots.at(pp->locallab.selspot).slomaskSH || slomaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskSH = pe->locallab.spots.at(pp->locallab.selspot).lapmaskSH || lapmaskSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).detailSH = pe->locallab.spots.at(pp->locallab.selspot).detailSH || detailSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).LmaskSHcurve = pe->locallab.spots.at(pp->locallab.selspot).LmaskSHcurve || !LmaskSHshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).shMethod = pe->locallab.spots.at(pp->locallab.selspot).shMethod || shMethod->get_active_text() != M("GENERAL_UNCHANGED");

                        for (int i = 0; i < 5; i++) {
                            pe->locallab.spots.at(pp->locallab.selspot).multsh[i] = pe->locallab.spots.at(pp->locallab.selspot).multsh[i] || multipliersh[i]->getEditedState();
                        }

                        pe->locallab.spots.at(pp->locallab.selspot).fatamountSH = pe->locallab.spots.at(pp->locallab.selspot).fatamountSH || fatamountSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fatanchorSH = pe->locallab.spots.at(pp->locallab.selspot).fatanchorSH || fatanchorSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gamSH = pe->locallab.spots.at(pp->locallab.selspot).gamSH || gamSH->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sloSH = pe->locallab.spots.at(pp->locallab.selspot).sloSH || sloSH->getEditedState();

                        // Vibrance
                        pe->locallab.spots.at(pp->locallab.selspot).expvibrance = pe->locallab.spots.at(pp->locallab.selspot).expvibrance || !expvibrance->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).saturated = pe->locallab.spots.at(pp->locallab.selspot).saturated || saturated->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).pastels = pe->locallab.spots.at(pp->locallab.selspot).pastels || pastels->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).psthreshold = pe->locallab.spots.at(pp->locallab.selspot).psthreshold || psThreshold->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).protectskins = pe->locallab.spots.at(pp->locallab.selspot).protectskins || !protectSkins->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).avoidcolorshift = pe->locallab.spots.at(pp->locallab.selspot).avoidcolorshift || !avoidColorShift->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).pastsattog = pe->locallab.spots.at(pp->locallab.selspot).pastsattog || !pastSatTog->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).sensiv = pe->locallab.spots.at(pp->locallab.selspot).sensiv || sensiv->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).skintonescurve = pe->locallab.spots.at(pp->locallab.selspot).skintonescurve || !skinTonesCurve->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskvibcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskvibcurve || !CCmaskvibshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskvibcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskvibcurve || !LLmaskvibshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskvibcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskvibcurve || !HHmaskvibshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).enavibMask = pe->locallab.spots.at(pp->locallab.selspot).enavibMask || !enavibMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskvib = pe->locallab.spots.at(pp->locallab.selspot).blendmaskvib || blendmaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskvib = pe->locallab.spots.at(pp->locallab.selspot).radmaskvib || radmaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskvib = pe->locallab.spots.at(pp->locallab.selspot).chromaskvib || chromaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskvib = pe->locallab.spots.at(pp->locallab.selspot).gammaskvib || gammaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskvib = pe->locallab.spots.at(pp->locallab.selspot).slomaskvib || slomaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskvib = pe->locallab.spots.at(pp->locallab.selspot).lapmaskvib || lapmaskvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskvibcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskvibcurve || !Lmaskvibshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).strvib = pe->locallab.spots.at(pp->locallab.selspot).strvib || strvib->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strvibab = pe->locallab.spots.at(pp->locallab.selspot).strvibab || strvibab->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strvibh = pe->locallab.spots.at(pp->locallab.selspot).strvibh || strvibh->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).angvib = pe->locallab.spots.at(pp->locallab.selspot).angvib || angvib->getEditedState();
                        // Soft Light
                        pe->locallab.spots.at(pp->locallab.selspot).expsoft = pe->locallab.spots.at(pp->locallab.selspot).expsoft || !expsoft->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).streng = pe->locallab.spots.at(pp->locallab.selspot).streng || streng->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensisf = pe->locallab.spots.at(pp->locallab.selspot).sensisf || sensisf->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).laplace = pe->locallab.spots.at(pp->locallab.selspot).laplace || laplace->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).softMethod = pe->locallab.spots.at(pp->locallab.selspot).softMethod || softMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        // Blur & Noise
                        pe->locallab.spots.at(pp->locallab.selspot).expblur = pe->locallab.spots.at(pp->locallab.selspot).expblur || !expblur->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).radius = pe->locallab.spots.at(pp->locallab.selspot).radius || radius->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strength = pe->locallab.spots.at(pp->locallab.selspot).strength || strength->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensibn = pe->locallab.spots.at(pp->locallab.selspot).sensibn || sensibn->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).itera = pe->locallab.spots.at(pp->locallab.selspot).itera || itera->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).isogr = pe->locallab.spots.at(pp->locallab.selspot).isogr || isogr->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strengr = pe->locallab.spots.at(pp->locallab.selspot).strengr || strengr->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).scalegr = pe->locallab.spots.at(pp->locallab.selspot).scalegr || scalegr->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).guidbl = pe->locallab.spots.at(pp->locallab.selspot).guidbl || guidbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).epsbl = pe->locallab.spots.at(pp->locallab.selspot).epsbl || epsbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blMethod = pe->locallab.spots.at(pp->locallab.selspot).blMethod || blMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).blurMethod = pe->locallab.spots.at(pp->locallab.selspot).blurMethod || blurMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).medMethod = pe->locallab.spots.at(pp->locallab.selspot).medMethod || medMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).activlum = pe->locallab.spots.at(pp->locallab.selspot).activlum || !activlum->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskblcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskblcurve || !CCmaskblshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskblcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskblcurve || !LLmaskblshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskblcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskblcurve || !HHmaskblshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).enablMask = pe->locallab.spots.at(pp->locallab.selspot).enablMask || !enablMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskbl = pe->locallab.spots.at(pp->locallab.selspot).blendmaskbl || blendmaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskbl = pe->locallab.spots.at(pp->locallab.selspot).radmaskbl || radmaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskbl = pe->locallab.spots.at(pp->locallab.selspot).chromaskbl || chromaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskbl = pe->locallab.spots.at(pp->locallab.selspot).gammaskbl || gammaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskbl = pe->locallab.spots.at(pp->locallab.selspot).slomaskbl || slomaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskbl = pe->locallab.spots.at(pp->locallab.selspot).lapmaskbl || lapmaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shadmaskbl = pe->locallab.spots.at(pp->locallab.selspot).shadmaskbl || shadmaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).strumaskbl = pe->locallab.spots.at(pp->locallab.selspot).strumaskbl || strumaskbl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fftwbl = pe->locallab.spots.at(pp->locallab.selspot).fftwbl || !fftwbl->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).toolbl = pe->locallab.spots.at(pp->locallab.selspot).toolbl || !toolbl->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskblcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskblcurve || !Lmaskblshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskblcurvewav = pe->locallab.spots.at(pp->locallab.selspot).LLmaskblcurvewav || !LLmaskblshapewav->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).csthresholdblur = pe->locallab.spots.at(pp->locallab.selspot).csthresholdblur || csThresholdblur->getEditedState();
                        // Tone Mapping
                        pe->locallab.spots.at(pp->locallab.selspot).exptonemap = pe->locallab.spots.at(pp->locallab.selspot).activlum || !exptonemap->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).stren = pe->locallab.spots.at(pp->locallab.selspot).stren || stren->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gamma = pe->locallab.spots.at(pp->locallab.selspot).gamma || gamma->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).estop = pe->locallab.spots.at(pp->locallab.selspot).estop || estop->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).scaltm = pe->locallab.spots.at(pp->locallab.selspot).scaltm || scaltm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).rewei = pe->locallab.spots.at(pp->locallab.selspot).rewei || rewei->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).satur = pe->locallab.spots.at(pp->locallab.selspot).satur || satur->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensitm = pe->locallab.spots.at(pp->locallab.selspot).sensitm || sensitm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).softradiustm = pe->locallab.spots.at(pp->locallab.selspot).softradiustm || softradiustm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).amount = pe->locallab.spots.at(pp->locallab.selspot).amount || amount->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).equiltm = pe->locallab.spots.at(pp->locallab.selspot).equiltm || !equiltm->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmasktmcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmasktmcurve || !CCmasktmshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmasktmcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmasktmcurve || !LLmasktmshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmasktmcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmasktmcurve || !HHmasktmshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).enatmMask = pe->locallab.spots.at(pp->locallab.selspot).enatmMask || !enatmMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).enatmMaskaft = pe->locallab.spots.at(pp->locallab.selspot).enatmMaskaft || !enatmMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmasktm = pe->locallab.spots.at(pp->locallab.selspot).blendmasktm || blendmasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmasktm = pe->locallab.spots.at(pp->locallab.selspot).radmasktm || radmasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromasktm = pe->locallab.spots.at(pp->locallab.selspot).chromasktm || chromasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammasktm = pe->locallab.spots.at(pp->locallab.selspot).gammasktm || gammasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomasktm = pe->locallab.spots.at(pp->locallab.selspot).slomasktm || slomasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmasktm = pe->locallab.spots.at(pp->locallab.selspot).lapmasktm || lapmasktm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmasktmcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmasktmcurve || !Lmasktmshape->isUnChanged();
                        // Retinex
                        pe->locallab.spots.at(pp->locallab.selspot).expreti = pe->locallab.spots.at(pp->locallab.selspot).expreti || !expreti->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).retinexMethod = pe->locallab.spots.at(pp->locallab.selspot).retinexMethod || retinexMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).str = pe->locallab.spots.at(pp->locallab.selspot).str || str->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chrrt = pe->locallab.spots.at(pp->locallab.selspot).chrrt || chrrt->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).neigh = pe->locallab.spots.at(pp->locallab.selspot).neigh || neigh->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).vart = pe->locallab.spots.at(pp->locallab.selspot).vart || vart->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).offs = pe->locallab.spots.at(pp->locallab.selspot).offs || offs->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).dehaz = pe->locallab.spots.at(pp->locallab.selspot).dehaz || dehaz->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).depth = pe->locallab.spots.at(pp->locallab.selspot).depth || depth->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensih = pe->locallab.spots.at(pp->locallab.selspot).sensih || sensih->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).localTgaincurve = pe->locallab.spots.at(pp->locallab.selspot).localTgaincurve || !cTgainshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).localTtranscurve = pe->locallab.spots.at(pp->locallab.selspot).localTtranscurve || !cTtransshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).inversret = pe->locallab.spots.at(pp->locallab.selspot).inversret || !inversret->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).equilret = pe->locallab.spots.at(pp->locallab.selspot).equilret || !equilret->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).loglin = pe->locallab.spots.at(pp->locallab.selspot).loglin || !loglin->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).lumonly = pe->locallab.spots.at(pp->locallab.selspot).lumonly || !lumonly->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).softradiusret = pe->locallab.spots.at(pp->locallab.selspot).softradiusret || softradiusret->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskreticurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskreticurve || !CCmaskretishape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskreticurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskreticurve || !LLmaskretishape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskreticurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskreticurve || !HHmaskretishape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).enaretiMask = pe->locallab.spots.at(pp->locallab.selspot).enaretiMask || !enaretiMask->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap = pe->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap || !enaretiMasktmap->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskreti = pe->locallab.spots.at(pp->locallab.selspot).blendmaskreti || blendmaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskreti = pe->locallab.spots.at(pp->locallab.selspot).radmaskreti || radmaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskreti = pe->locallab.spots.at(pp->locallab.selspot).chromaskreti || chromaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskreti = pe->locallab.spots.at(pp->locallab.selspot).gammaskreti || gammaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskreti = pe->locallab.spots.at(pp->locallab.selspot).slomaskreti || slomaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskreti = pe->locallab.spots.at(pp->locallab.selspot).lapmaskreti || lapmaskreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).scalereti = pe->locallab.spots.at(pp->locallab.selspot).scalereti || scalereti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).darkness = pe->locallab.spots.at(pp->locallab.selspot).darkness || darkness->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lightnessreti = pe->locallab.spots.at(pp->locallab.selspot).lightnessreti || lightnessreti->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).limd = pe->locallab.spots.at(pp->locallab.selspot).limd || limd->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).cliptm = pe->locallab.spots.at(pp->locallab.selspot).cliptm || cliptm->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fftwreti = pe->locallab.spots.at(pp->locallab.selspot).fftwreti || !fftwreti->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskreticurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskreticurve || !Lmaskretishape->isUnChanged();
                        // Sharpening
                        pe->locallab.spots.at(pp->locallab.selspot).expsharp = pe->locallab.spots.at(pp->locallab.selspot).expsharp || !expsharp->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).sharcontrast = pe->locallab.spots.at(pp->locallab.selspot).sharcontrast || sharcontrast->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sharradius = pe->locallab.spots.at(pp->locallab.selspot).sharradius || sharradius->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sharamount = pe->locallab.spots.at(pp->locallab.selspot).sharamount || sharamount->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shardamping = pe->locallab.spots.at(pp->locallab.selspot).shardamping || shardamping->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).shariter = pe->locallab.spots.at(pp->locallab.selspot).shariter || shariter->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sharblur = pe->locallab.spots.at(pp->locallab.selspot).sharblur || sharblur->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensisha = pe->locallab.spots.at(pp->locallab.selspot).sensisha || sensisha->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).inverssha = pe->locallab.spots.at(pp->locallab.selspot).inverssha || !inverssha->get_inconsistent();
                        // Local Contrast
                        pe->locallab.spots.at(pp->locallab.selspot).expcontrast = pe->locallab.spots.at(pp->locallab.selspot).expcontrast || !expcontrast->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).lcradius = pe->locallab.spots.at(pp->locallab.selspot).lcradius || lcradius->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lcamount = pe->locallab.spots.at(pp->locallab.selspot).lcamount || lcamount->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lcdarkness = pe->locallab.spots.at(pp->locallab.selspot).lcdarkness || lcdarkness->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lclightness = pe->locallab.spots.at(pp->locallab.selspot).lclightness || lclightness->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).levelwav = pe->locallab.spots.at(pp->locallab.selspot).levelwav || levelwav->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).residcont = pe->locallab.spots.at(pp->locallab.selspot).residcont || residcont->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).residchro = pe->locallab.spots.at(pp->locallab.selspot).residchro || residchro->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).clarilres = pe->locallab.spots.at(pp->locallab.selspot).clarilres || clarilres->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).claricres = pe->locallab.spots.at(pp->locallab.selspot).claricres || claricres->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).clarisoft = pe->locallab.spots.at(pp->locallab.selspot).clarisoft || clarisoft->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensilc = pe->locallab.spots.at(pp->locallab.selspot).sensilc || sensilc->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).fftwlc = pe->locallab.spots.at(pp->locallab.selspot).fftwlc || !fftwlc->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).localcontMethod = pe->locallab.spots.at(pp->locallab.selspot).localcontMethod || localcontMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pe->locallab.spots.at(pp->locallab.selspot).locwavcurve = pe->locallab.spots.at(pp->locallab.selspot).locwavcurve || !wavshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).csthreshold = pe->locallab.spots.at(pp->locallab.selspot).csthreshold || csThreshold->getEditedState();
                        // Contrast by detail levels
                        pe->locallab.spots.at(pp->locallab.selspot).expcbdl = pe->locallab.spots.at(pp->locallab.selspot).expcbdl || !expcbdl->get_inconsistent();

                        for (int i = 0; i < 6; i++) {
                            pe->locallab.spots.at(pp->locallab.selspot).mult[i] = pe->locallab.spots.at(pp->locallab.selspot).mult[i] || multiplier[i]->getEditedState();
                        }

                        pe->locallab.spots.at(pp->locallab.selspot).chromacbdl = pe->locallab.spots.at(pp->locallab.selspot).chromacbdl || chromacbdl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).threshold = pe->locallab.spots.at(pp->locallab.selspot).threshold || threshold->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensicb = pe->locallab.spots.at(pp->locallab.selspot).sensicb || sensicb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).clarityml = pe->locallab.spots.at(pp->locallab.selspot).clarityml || clarityml->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).contresid = pe->locallab.spots.at(pp->locallab.selspot).contresid || contresid->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).blurcbdl = pe->locallab.spots.at(pp->locallab.selspot).blurcbdl || blurcbdl->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).softradiuscb = pe->locallab.spots.at(pp->locallab.selspot).softradiuscb || softradiuscb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).enacbMask = pe->locallab.spots.at(pp->locallab.selspot).enacbMask || !enacbMask->get_inconsistent();

                        pe->locallab.spots.at(pp->locallab.selspot).CCmaskcbcurve = pe->locallab.spots.at(pp->locallab.selspot).CCmaskcbcurve || !CCmaskcbshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).LLmaskcbcurve = pe->locallab.spots.at(pp->locallab.selspot).LLmaskcbcurve || !LLmaskcbshape->isUnChanged();
                        pe->locallab.spots.at(pp->locallab.selspot).HHmaskcbcurve = pe->locallab.spots.at(pp->locallab.selspot).HHmaskcbcurve || !HHmaskcbshape->isUnChanged();

                        pe->locallab.spots.at(pp->locallab.selspot).blendmaskcb = pe->locallab.spots.at(pp->locallab.selspot).blendmaskcb || blendmaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).radmaskcb = pe->locallab.spots.at(pp->locallab.selspot).radmaskcb || radmaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).chromaskcb = pe->locallab.spots.at(pp->locallab.selspot).chromaskcb || chromaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).gammaskcb = pe->locallab.spots.at(pp->locallab.selspot).gammaskcb || gammaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).slomaskcb = pe->locallab.spots.at(pp->locallab.selspot).slomaskcb || slomaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).lapmaskcb = pe->locallab.spots.at(pp->locallab.selspot).lapmaskcb || lapmaskcb->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).Lmaskcbcurve = pe->locallab.spots.at(pp->locallab.selspot).Lmaskcbcurve || !Lmaskcbshape->isUnChanged();

                        // Denoise
                        pe->locallab.spots.at(pp->locallab.selspot).expdenoi = pe->locallab.spots.at(pp->locallab.selspot).expdenoi || !expdenoi->get_inconsistent();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselumf = pe->locallab.spots.at(pp->locallab.selspot).noiselumf || noiselumf->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselumf0 = pe->locallab.spots.at(pp->locallab.selspot).noiselumf0 || noiselumf0->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselumf2 = pe->locallab.spots.at(pp->locallab.selspot).noiselumf2 || noiselumf2->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselumc = pe->locallab.spots.at(pp->locallab.selspot).noiselumc || noiselumc->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselumdetail = pe->locallab.spots.at(pp->locallab.selspot).noiselumdetail || noiselumdetail->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noiselequal = pe->locallab.spots.at(pp->locallab.selspot).noiselequal || noiselequal->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noisechrof = pe->locallab.spots.at(pp->locallab.selspot).noisechrof || noisechrof->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noisechroc = pe->locallab.spots.at(pp->locallab.selspot).noisechroc || noisechroc->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).noisechrodetail = pe->locallab.spots.at(pp->locallab.selspot).noisechrodetail || noisechrodetail->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).adjblur = pe->locallab.spots.at(pp->locallab.selspot).adjblur || adjblur->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).bilateral = pe->locallab.spots.at(pp->locallab.selspot).bilateral || bilateral->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).sensiden = pe->locallab.spots.at(pp->locallab.selspot).sensiden || sensiden->getEditedState();
                        pe->locallab.spots.at(pp->locallab.selspot).detailthr = pe->locallab.spots.at(pp->locallab.selspot).detailthr || detailthr->getEditedState();
                    }
                }

                // ParamsEdited update
                if (pedited) {
                    pedited->locallab.enabled = pedited->locallab.enabled || !get_inconsistent();

                    if (pp->locallab.selspot < (int)pedited->locallab.spots.size()) {
                        // Control spot settings
                        pedited->locallab.spots.at(pp->locallab.selspot).name = pedited->locallab.spots.at(pp->locallab.selspot).name || se->name;
                        pedited->locallab.spots.at(pp->locallab.selspot).isvisible = pedited->locallab.spots.at(pp->locallab.selspot).isvisible || se->isvisible;
                        pedited->locallab.spots.at(pp->locallab.selspot).shape = pedited->locallab.spots.at(pp->locallab.selspot).shape || se->shape;
                        pedited->locallab.spots.at(pp->locallab.selspot).spotMethod = pedited->locallab.spots.at(pp->locallab.selspot).spotMethod || se->spotMethod;
//                        pedited->locallab.spots.at(pp->locallab.selspot).mergeMethod = pedited->locallab.spots.at(pp->locallab.selspot).mergeMethod || se->mergeMethod;
                        pedited->locallab.spots.at(pp->locallab.selspot).sensiexclu = pedited->locallab.spots.at(pp->locallab.selspot).sensiexclu || se->sensiexclu;
                        pedited->locallab.spots.at(pp->locallab.selspot).structexclu = pedited->locallab.spots.at(pp->locallab.selspot).structexclu || se->structexclu;
                        pedited->locallab.spots.at(pp->locallab.selspot).struc = pedited->locallab.spots.at(pp->locallab.selspot).struc || se->struc;
                        pedited->locallab.spots.at(pp->locallab.selspot).shapeMethod = pedited->locallab.spots.at(pp->locallab.selspot).shapeMethod || se->shapeMethod;
                        pedited->locallab.spots.at(pp->locallab.selspot).locX = pedited->locallab.spots.at(pp->locallab.selspot).locX || se->locX;
                        pedited->locallab.spots.at(pp->locallab.selspot).locXL = pedited->locallab.spots.at(pp->locallab.selspot).locXL || se->locXL;
                        pedited->locallab.spots.at(pp->locallab.selspot).locY = pedited->locallab.spots.at(pp->locallab.selspot).locY || se->locY;
                        pedited->locallab.spots.at(pp->locallab.selspot).locYT = pedited->locallab.spots.at(pp->locallab.selspot).locYT || se->locYT;
                        pedited->locallab.spots.at(pp->locallab.selspot).centerX = pedited->locallab.spots.at(pp->locallab.selspot).centerX || se->centerX;
                        pedited->locallab.spots.at(pp->locallab.selspot).centerY = pedited->locallab.spots.at(pp->locallab.selspot).centerY || se->centerY;
                        pedited->locallab.spots.at(pp->locallab.selspot).circrad = pedited->locallab.spots.at(pp->locallab.selspot).circrad || se->circrad;
                        pedited->locallab.spots.at(pp->locallab.selspot).qualityMethod = pedited->locallab.spots.at(pp->locallab.selspot).qualityMethod || se->qualityMethod;
                        pedited->locallab.spots.at(pp->locallab.selspot).complexMethod = pedited->locallab.spots.at(pp->locallab.selspot).complexMethod || se->complexMethod;
                        pedited->locallab.spots.at(pp->locallab.selspot).transit = pedited->locallab.spots.at(pp->locallab.selspot).transit || se->transit;
                        pedited->locallab.spots.at(pp->locallab.selspot).feather = pedited->locallab.spots.at(pp->locallab.selspot).feather || se->feather;
                        pedited->locallab.spots.at(pp->locallab.selspot).thresh = pedited->locallab.spots.at(pp->locallab.selspot).thresh || se->thresh;
                        pedited->locallab.spots.at(pp->locallab.selspot).iter = pedited->locallab.spots.at(pp->locallab.selspot).iter || se->iter;
                        pedited->locallab.spots.at(pp->locallab.selspot).balan = pedited->locallab.spots.at(pp->locallab.selspot).balan || se->balan;
                        pedited->locallab.spots.at(pp->locallab.selspot).transitweak = pedited->locallab.spots.at(pp->locallab.selspot).transitweak || se->transitweak;
                        pedited->locallab.spots.at(pp->locallab.selspot).transitgrad = pedited->locallab.spots.at(pp->locallab.selspot).transitgrad || se->transitgrad;
                        pedited->locallab.spots.at(pp->locallab.selspot).scopemask = pedited->locallab.spots.at(pp->locallab.selspot).scopemask || se->scopemask;
                        pedited->locallab.spots.at(pp->locallab.selspot).lumask = pedited->locallab.spots.at(pp->locallab.selspot).lumask || se->lumask;
                        pedited->locallab.spots.at(pp->locallab.selspot).avoid = pedited->locallab.spots.at(pp->locallab.selspot).avoid || se->avoid;
                        pedited->locallab.spots.at(pp->locallab.selspot).recurs = pedited->locallab.spots.at(pp->locallab.selspot).recurs || se->recurs;
                        pedited->locallab.spots.at(pp->locallab.selspot).laplac = pedited->locallab.spots.at(pp->locallab.selspot).laplac || se->laplac;
                        pedited->locallab.spots.at(pp->locallab.selspot).deltae = pedited->locallab.spots.at(pp->locallab.selspot).deltae || se->deltae;
                        pedited->locallab.spots.at(pp->locallab.selspot).shortc = pedited->locallab.spots.at(pp->locallab.selspot).shortc || se->shortc;
                        pedited->locallab.spots.at(pp->locallab.selspot).savrest = pedited->locallab.spots.at(pp->locallab.selspot).savrest || se->savrest;
                        // Color & Light
                        pedited->locallab.spots.at(pp->locallab.selspot).expcolor = pedited->locallab.spots.at(pp->locallab.selspot).expcolor || !expcolor->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).curvactiv = pedited->locallab.spots.at(pp->locallab.selspot).curvactiv || !curvactiv->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).lightness = pedited->locallab.spots.at(pp->locallab.selspot).lightness || lightness->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).contrast = pedited->locallab.spots.at(pp->locallab.selspot).contrast || contrast->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chroma = pedited->locallab.spots.at(pp->locallab.selspot).chroma || chroma->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strengthgrid = pedited->locallab.spots.at(pp->locallab.selspot).strengthgrid || strengthgrid->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensi = pedited->locallab.spots.at(pp->locallab.selspot).sensi || sensi->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).structcol = pedited->locallab.spots.at(pp->locallab.selspot).structcol || structcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strcol = pedited->locallab.spots.at(pp->locallab.selspot).strcol || strcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strcolab = pedited->locallab.spots.at(pp->locallab.selspot).strcolab || strcolab->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strcolh = pedited->locallab.spots.at(pp->locallab.selspot).strcolh || strcolh->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).angcol = pedited->locallab.spots.at(pp->locallab.selspot).angcol || angcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod = pedited->locallab.spots.at(pp->locallab.selspot).qualitycurveMethod || qualitycurveMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).gridMethod = pedited->locallab.spots.at(pp->locallab.selspot).gridMethod || gridMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).merMethod = pedited->locallab.spots.at(pp->locallab.selspot).merMethod || merMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).toneMethod = pedited->locallab.spots.at(pp->locallab.selspot).toneMethod || toneMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).mergecolMethod = pedited->locallab.spots.at(pp->locallab.selspot).mergecolMethod || mergecolMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).llcurve = pedited->locallab.spots.at(pp->locallab.selspot).llcurve || !llshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).lccurve = pedited->locallab.spots.at(pp->locallab.selspot).lccurve || !lcshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).rgbcurve = pedited->locallab.spots.at(pp->locallab.selspot).rgbcurve || !rgbshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).cccurve = pedited->locallab.spots.at(pp->locallab.selspot).cccurve || !ccshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).clcurve = pedited->locallab.spots.at(pp->locallab.selspot).clcurve || !clshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).lccurve = pedited->locallab.spots.at(pp->locallab.selspot).lccurve || !lcshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LHcurve = pedited->locallab.spots.at(pp->locallab.selspot).LHcurve || !LHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHcurve || !HHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).invers = pedited->locallab.spots.at(pp->locallab.selspot).invers || !invers->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).special = pedited->locallab.spots.at(pp->locallab.selspot).special || !special->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).toolcol = pedited->locallab.spots.at(pp->locallab.selspot).toolcol || !toolcol->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaColorMask = pedited->locallab.spots.at(pp->locallab.selspot).enaColorMask || !enaColorMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).fftColorMask = pedited->locallab.spots.at(pp->locallab.selspot).fftColorMask || !fftColorMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskcurve || !CCmaskshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcurve || !LLmaskshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskcurve || !HHmaskshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHhmaskcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHhmaskcurve || !HHhmaskshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).blurcolde = pedited->locallab.spots.at(pp->locallab.selspot).blurcolde || blurcolde->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blurcol = pedited->locallab.spots.at(pp->locallab.selspot).blurcol || blurcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).contcol = pedited->locallab.spots.at(pp->locallab.selspot).contcol || contcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskcol = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskcol || blendmaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskcol = pedited->locallab.spots.at(pp->locallab.selspot).radmaskcol || radmaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskcol = pedited->locallab.spots.at(pp->locallab.selspot).chromaskcol || chromaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskcol = pedited->locallab.spots.at(pp->locallab.selspot).gammaskcol || gammaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskcol = pedited->locallab.spots.at(pp->locallab.selspot).slomaskcol || slomaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shadmaskcol = pedited->locallab.spots.at(pp->locallab.selspot).shadmaskcol || shadmaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strumaskcol = pedited->locallab.spots.at(pp->locallab.selspot).strumaskcol || strumaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskcol = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskcol || lapmaskcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).softradiuscol = pedited->locallab.spots.at(pp->locallab.selspot).softradiuscol || softradiuscol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).opacol = pedited->locallab.spots.at(pp->locallab.selspot).opacol || opacol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).mercol = pedited->locallab.spots.at(pp->locallab.selspot).mercol || mercol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).merlucol = pedited->locallab.spots.at(pp->locallab.selspot).merlucol || merlucol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).conthrcol = pedited->locallab.spots.at(pp->locallab.selspot).conthrcol || conthrcol->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskcurve || !Lmaskshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcolcurvewav = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcolcurvewav || !LLmaskcolshapewav->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).csthresholdcol = pedited->locallab.spots.at(pp->locallab.selspot).csthresholdcol || csThresholdcol->getEditedState();
                        // Exposure
                        pedited->locallab.spots.at(pp->locallab.selspot).expexpose = pedited->locallab.spots.at(pp->locallab.selspot).expexpose || !expexpose->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).expcomp = pedited->locallab.spots.at(pp->locallab.selspot).expcomp || expcomp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).hlcompr = pedited->locallab.spots.at(pp->locallab.selspot).hlcompr || hlcompr->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).hlcomprthresh = pedited->locallab.spots.at(pp->locallab.selspot).hlcomprthresh || hlcomprthresh->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).black = pedited->locallab.spots.at(pp->locallab.selspot).black || black->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shadex = pedited->locallab.spots.at(pp->locallab.selspot).shadex || shadex->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shcompr = pedited->locallab.spots.at(pp->locallab.selspot).shcompr || shcompr->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).expchroma = pedited->locallab.spots.at(pp->locallab.selspot).expchroma || expchroma->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).warm = pedited->locallab.spots.at(pp->locallab.selspot).warm || warm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensiex = pedited->locallab.spots.at(pp->locallab.selspot).sensiex || sensiex->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).structexp = pedited->locallab.spots.at(pp->locallab.selspot).structexp || structexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blurexpde = pedited->locallab.spots.at(pp->locallab.selspot).blurexpde || blurexpde->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strexp = pedited->locallab.spots.at(pp->locallab.selspot).strexp || strexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).angexp = pedited->locallab.spots.at(pp->locallab.selspot).angexp || angexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).excurve = pedited->locallab.spots.at(pp->locallab.selspot).excurve || !shapeexpos->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).inversex = pedited->locallab.spots.at(pp->locallab.selspot).inversex || !inversex->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaExpMask = pedited->locallab.spots.at(pp->locallab.selspot).enaExpMask || !enaExpMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaExpMaskaft = pedited->locallab.spots.at(pp->locallab.selspot).enaExpMaskaft || !enaExpMaskaft->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskexpcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskexpcurve || !CCmaskexpshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskexpcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskexpcurve || !LLmaskexpshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskexpcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskexpcurve || !HHmaskexpshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskexp = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskexp || blendmaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskexp = pedited->locallab.spots.at(pp->locallab.selspot).radmaskexp || radmaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskexp = pedited->locallab.spots.at(pp->locallab.selspot).chromaskexp || chromaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskexp = pedited->locallab.spots.at(pp->locallab.selspot).gammaskexp || gammaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskexp = pedited->locallab.spots.at(pp->locallab.selspot).slomaskexp || slomaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskexp = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskexp || lapmaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strmaskexp = pedited->locallab.spots.at(pp->locallab.selspot).strmaskexp || strmaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).angmaskexp = pedited->locallab.spots.at(pp->locallab.selspot).angmaskexp || angmaskexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).softradiusexp = pedited->locallab.spots.at(pp->locallab.selspot).softradiusexp || softradiusexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskexpcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskexpcurve || !Lmaskexpshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).expMethod = pedited->locallab.spots.at(pp->locallab.selspot).expMethod || expMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).exnoiseMethod = pedited->locallab.spots.at(pp->locallab.selspot).exnoiseMethod || exnoiseMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).laplacexp = pedited->locallab.spots.at(pp->locallab.selspot).laplacexp || laplacexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).balanexp = pedited->locallab.spots.at(pp->locallab.selspot).balanexp || balanexp->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).linear = pedited->locallab.spots.at(pp->locallab.selspot).linear || linear->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gamm = pedited->locallab.spots.at(pp->locallab.selspot).gamm || gamm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fatamount = pedited->locallab.spots.at(pp->locallab.selspot).fatamount || fatamount->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fatdetail = pedited->locallab.spots.at(pp->locallab.selspot).fatdetail || fatdetail->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fatanchor = pedited->locallab.spots.at(pp->locallab.selspot).fatanchor || fatanchor->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fatlevel = pedited->locallab.spots.at(pp->locallab.selspot).fatlevel || fatlevel->getEditedState();
                        // Shadow highlight
                        pedited->locallab.spots.at(pp->locallab.selspot).expshadhigh = pedited->locallab.spots.at(pp->locallab.selspot).expshadhigh || !expshadhigh->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).highlights = pedited->locallab.spots.at(pp->locallab.selspot).highlights || highlights->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).h_tonalwidth = pedited->locallab.spots.at(pp->locallab.selspot).h_tonalwidth || h_tonalwidth->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shadows = pedited->locallab.spots.at(pp->locallab.selspot).shadows || shadows->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).s_tonalwidth = pedited->locallab.spots.at(pp->locallab.selspot).s_tonalwidth || s_tonalwidth->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sh_radius = pedited->locallab.spots.at(pp->locallab.selspot).sh_radius || sh_radius->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensihs = pedited->locallab.spots.at(pp->locallab.selspot).sensihs || sensihs->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaSHMask = pedited->locallab.spots.at(pp->locallab.selspot).enaSHMask || !enaSHMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskSHcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskSHcurve || !CCmaskSHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskSHcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskSHcurve || !LLmaskSHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskSHcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskSHcurve || !HHmaskSHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskSH = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskSH || blendmaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskSH = pedited->locallab.spots.at(pp->locallab.selspot).radmaskSH || radmaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blurSHde = pedited->locallab.spots.at(pp->locallab.selspot).blurSHde || blurSHde->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strSH = pedited->locallab.spots.at(pp->locallab.selspot).strSH || strSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).angSH = pedited->locallab.spots.at(pp->locallab.selspot).angSH || angSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).inverssh = pedited->locallab.spots.at(pp->locallab.selspot).inverssh || !inverssh->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskSH = pedited->locallab.spots.at(pp->locallab.selspot).chromaskSH || chromaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskSH = pedited->locallab.spots.at(pp->locallab.selspot).gammaskSH || gammaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskSH = pedited->locallab.spots.at(pp->locallab.selspot).slomaskSH || slomaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskSH = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskSH || lapmaskSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).detailSH = pedited->locallab.spots.at(pp->locallab.selspot).detailSH || detailSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).LmaskSHcurve = pedited->locallab.spots.at(pp->locallab.selspot).LmaskSHcurve || !LmaskSHshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).shMethod = pedited->locallab.spots.at(pp->locallab.selspot).shMethod || shMethod->get_active_text() != M("GENERAL_UNCHANGED");

                        for (int i = 0; i < 5; i++) {
                            pedited->locallab.spots.at(pp->locallab.selspot).multsh[i] = pedited->locallab.spots.at(pp->locallab.selspot).multsh[i] || multipliersh[i]->getEditedState();
                        }

                        pedited->locallab.spots.at(pp->locallab.selspot).fatamountSH = pedited->locallab.spots.at(pp->locallab.selspot).fatamountSH || fatamountSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fatanchorSH = pedited->locallab.spots.at(pp->locallab.selspot).fatanchorSH || fatanchorSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gamSH = pedited->locallab.spots.at(pp->locallab.selspot).gamSH || gamSH->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sloSH = pedited->locallab.spots.at(pp->locallab.selspot).sloSH || sloSH->getEditedState();

                        // Vibrance
                        pedited->locallab.spots.at(pp->locallab.selspot).expvibrance = pedited->locallab.spots.at(pp->locallab.selspot).expvibrance || !expvibrance->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).saturated = pedited->locallab.spots.at(pp->locallab.selspot).saturated || saturated->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).pastels = pedited->locallab.spots.at(pp->locallab.selspot).pastels || pastels->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).psthreshold = pedited->locallab.spots.at(pp->locallab.selspot).psthreshold || psThreshold->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).protectskins = pedited->locallab.spots.at(pp->locallab.selspot).protectskins || !protectSkins->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).avoidcolorshift = pedited->locallab.spots.at(pp->locallab.selspot).avoidcolorshift || !avoidColorShift->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).pastsattog = pedited->locallab.spots.at(pp->locallab.selspot).pastsattog || !pastSatTog->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensiv = pedited->locallab.spots.at(pp->locallab.selspot).sensiv || sensiv->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).skintonescurve = pedited->locallab.spots.at(pp->locallab.selspot).skintonescurve || !skinTonesCurve->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskvibcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskvibcurve || !CCmaskvibshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskvibcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskvibcurve || !LLmaskvibshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskvibcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskvibcurve || !HHmaskvibshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).enavibMask = pedited->locallab.spots.at(pp->locallab.selspot).enavibMask || !enavibMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskvib = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskvib || blendmaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskvib = pedited->locallab.spots.at(pp->locallab.selspot).radmaskvib || radmaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskvib = pedited->locallab.spots.at(pp->locallab.selspot).chromaskvib || chromaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskvib = pedited->locallab.spots.at(pp->locallab.selspot).gammaskvib || gammaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskvib = pedited->locallab.spots.at(pp->locallab.selspot).slomaskvib || slomaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskvib = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskvib || lapmaskvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskvibcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskvibcurve || !Lmaskvibshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).strvib = pedited->locallab.spots.at(pp->locallab.selspot).strvib || strvib->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strvibab = pedited->locallab.spots.at(pp->locallab.selspot).strvibab || strvibab->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strvibh = pedited->locallab.spots.at(pp->locallab.selspot).strvibh || strvibh->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).angvib = pedited->locallab.spots.at(pp->locallab.selspot).angvib || angvib->getEditedState();
                        // Soft Light
                        pedited->locallab.spots.at(pp->locallab.selspot).expsoft = pedited->locallab.spots.at(pp->locallab.selspot).expsoft || !expsoft->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).streng = pedited->locallab.spots.at(pp->locallab.selspot).streng || streng->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensisf = pedited->locallab.spots.at(pp->locallab.selspot).sensisf || sensisf->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).laplace = pedited->locallab.spots.at(pp->locallab.selspot).laplace || laplace->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).softMethod = pedited->locallab.spots.at(pp->locallab.selspot).softMethod || softMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        // Blur & Noise
                        pedited->locallab.spots.at(pp->locallab.selspot).expblur = pedited->locallab.spots.at(pp->locallab.selspot).expblur || !expblur->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).radius = pedited->locallab.spots.at(pp->locallab.selspot).radius || radius->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strength = pedited->locallab.spots.at(pp->locallab.selspot).strength || strength->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensibn = pedited->locallab.spots.at(pp->locallab.selspot).sensibn || sensibn->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).itera = pedited->locallab.spots.at(pp->locallab.selspot).itera || itera->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).isogr = pedited->locallab.spots.at(pp->locallab.selspot).isogr || isogr->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strengr = pedited->locallab.spots.at(pp->locallab.selspot).strengr || strengr->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).scalegr = pedited->locallab.spots.at(pp->locallab.selspot).scalegr || scalegr->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).guidbl = pedited->locallab.spots.at(pp->locallab.selspot).guidbl || guidbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).epsbl = pedited->locallab.spots.at(pp->locallab.selspot).epsbl || epsbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blMethod = pedited->locallab.spots.at(pp->locallab.selspot).blMethod || blMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).blurMethod = pedited->locallab.spots.at(pp->locallab.selspot).blurMethod || blurMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).medMethod = pedited->locallab.spots.at(pp->locallab.selspot).medMethod || medMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).activlum = pedited->locallab.spots.at(pp->locallab.selspot).activlum || !activlum->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskblcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskblcurve || !CCmaskblshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskblcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskblcurve || !LLmaskblshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskblcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskblcurve || !HHmaskblshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).enablMask = pedited->locallab.spots.at(pp->locallab.selspot).enablMask || !enablMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskbl = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskbl || blendmaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskbl = pedited->locallab.spots.at(pp->locallab.selspot).radmaskbl || radmaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskbl = pedited->locallab.spots.at(pp->locallab.selspot).chromaskbl || chromaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskbl = pedited->locallab.spots.at(pp->locallab.selspot).gammaskbl || gammaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskbl = pedited->locallab.spots.at(pp->locallab.selspot).slomaskbl || slomaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskbl = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskbl || lapmaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shadmaskbl = pedited->locallab.spots.at(pp->locallab.selspot).shadmaskbl || shadmaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).strumaskbl = pedited->locallab.spots.at(pp->locallab.selspot).strumaskbl || strumaskbl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fftwbl = pedited->locallab.spots.at(pp->locallab.selspot).fftwbl || !fftwbl->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).toolbl = pedited->locallab.spots.at(pp->locallab.selspot).toolbl || !toolbl->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskblcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskblcurve || !Lmaskblshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskblcurvewav = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskblcurvewav || !LLmaskblshapewav->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).csthresholdblur = pedited->locallab.spots.at(pp->locallab.selspot).csthresholdblur || csThresholdblur->getEditedState();
                        // Tone Mapping
                        pedited->locallab.spots.at(pp->locallab.selspot).exptonemap = pedited->locallab.spots.at(pp->locallab.selspot).exptonemap || !exptonemap->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).stren = pedited->locallab.spots.at(pp->locallab.selspot).stren || stren->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gamma = pedited->locallab.spots.at(pp->locallab.selspot).gamma || gamma->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).estop = pedited->locallab.spots.at(pp->locallab.selspot).estop || estop->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).scaltm = pedited->locallab.spots.at(pp->locallab.selspot).scaltm || scaltm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).rewei = pedited->locallab.spots.at(pp->locallab.selspot).rewei || rewei->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).satur = pedited->locallab.spots.at(pp->locallab.selspot).satur || satur->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensitm = pedited->locallab.spots.at(pp->locallab.selspot).sensitm || sensitm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).softradiustm = pedited->locallab.spots.at(pp->locallab.selspot).softradiustm || softradiustm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).amount = pedited->locallab.spots.at(pp->locallab.selspot).amount || amount->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaretiMask = pedited->locallab.spots.at(pp->locallab.selspot).enaretiMask || !enaretiMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap = pedited->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap || !enaretiMasktmap->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).equiltm = pedited->locallab.spots.at(pp->locallab.selspot).equiltm || !equiltm->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmasktmcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmasktmcurve || !CCmasktmshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmasktmcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmasktmcurve || !LLmasktmshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmasktmcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmasktmcurve || !HHmasktmshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).enatmMask = pedited->locallab.spots.at(pp->locallab.selspot).enatmMask || !enatmMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enatmMaskaft = pedited->locallab.spots.at(pp->locallab.selspot).enatmMaskaft || !enatmMaskaft->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmasktm = pedited->locallab.spots.at(pp->locallab.selspot).blendmasktm || blendmasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmasktm = pedited->locallab.spots.at(pp->locallab.selspot).radmasktm || radmasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromasktm = pedited->locallab.spots.at(pp->locallab.selspot).chromasktm || chromasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammasktm = pedited->locallab.spots.at(pp->locallab.selspot).gammasktm || gammasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomasktm = pedited->locallab.spots.at(pp->locallab.selspot).slomasktm || slomasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmasktm = pedited->locallab.spots.at(pp->locallab.selspot).lapmasktm || lapmasktm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmasktmcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmasktmcurve || !Lmasktmshape->isUnChanged();
                        // Retinex
                        pedited->locallab.spots.at(pp->locallab.selspot).expreti = pedited->locallab.spots.at(pp->locallab.selspot).expreti || !expreti->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).retinexMethod = pedited->locallab.spots.at(pp->locallab.selspot).retinexMethod || retinexMethod->get_active_text() != M("GENERAL_UNCHANGED");
                        pedited->locallab.spots.at(pp->locallab.selspot).str = pedited->locallab.spots.at(pp->locallab.selspot).str || str->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chrrt = pedited->locallab.spots.at(pp->locallab.selspot).chrrt || chrrt->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).neigh = pedited->locallab.spots.at(pp->locallab.selspot).neigh || neigh->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).vart = pedited->locallab.spots.at(pp->locallab.selspot).vart || vart->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).offs = pedited->locallab.spots.at(pp->locallab.selspot).offs || offs->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).dehaz = pedited->locallab.spots.at(pp->locallab.selspot).dehaz || dehaz->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).depth = pedited->locallab.spots.at(pp->locallab.selspot).depth || depth->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensih = pedited->locallab.spots.at(pp->locallab.selspot).sensih || sensih->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).localTgaincurve = pedited->locallab.spots.at(pp->locallab.selspot).localTgaincurve || !cTgainshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).localTtranscurve = pedited->locallab.spots.at(pp->locallab.selspot).localTtranscurve || !cTtransshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).inversret = pedited->locallab.spots.at(pp->locallab.selspot).inversret || !inversret->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).equilret = pedited->locallab.spots.at(pp->locallab.selspot).equilret || !equilret->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).loglin = pedited->locallab.spots.at(pp->locallab.selspot).loglin || !loglin->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).lumonly = pedited->locallab.spots.at(pp->locallab.selspot).lumonly || !lumonly->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).softradiusret = pedited->locallab.spots.at(pp->locallab.selspot).softradiusret || softradiusret->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskreticurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskreticurve || !CCmaskretishape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskreticurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskreticurve || !LLmaskretishape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskreticurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskreticurve || !HHmaskretishape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaretiMask = pedited->locallab.spots.at(pp->locallab.selspot).enaretiMask || !enaretiMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap = pedited->locallab.spots.at(pp->locallab.selspot).enaretiMasktmap || !enaretiMasktmap->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskreti = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskreti || blendmaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskreti = pedited->locallab.spots.at(pp->locallab.selspot).radmaskreti || radmaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskreti = pedited->locallab.spots.at(pp->locallab.selspot).chromaskreti || chromaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskreti = pedited->locallab.spots.at(pp->locallab.selspot).gammaskreti || gammaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskreti = pedited->locallab.spots.at(pp->locallab.selspot).slomaskreti || slomaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskreti = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskreti || lapmaskreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).scalereti = pedited->locallab.spots.at(pp->locallab.selspot).scalereti || scalereti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).darkness = pedited->locallab.spots.at(pp->locallab.selspot).darkness || darkness->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lightnessreti = pedited->locallab.spots.at(pp->locallab.selspot).lightnessreti || lightnessreti->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).limd = pedited->locallab.spots.at(pp->locallab.selspot).limd || limd->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).cliptm = pedited->locallab.spots.at(pp->locallab.selspot).cliptm || cliptm->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fftwreti = pedited->locallab.spots.at(pp->locallab.selspot).fftwreti || !fftwreti->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskreticurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskreticurve || !Lmaskretishape->isUnChanged();
                        // Sharpening
                        pedited->locallab.spots.at(pp->locallab.selspot).expsharp = pedited->locallab.spots.at(pp->locallab.selspot).expsharp || !expsharp->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).sharcontrast = pedited->locallab.spots.at(pp->locallab.selspot).sharcontrast || sharcontrast->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sharradius = pedited->locallab.spots.at(pp->locallab.selspot).sharradius || sharradius->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sharamount = pedited->locallab.spots.at(pp->locallab.selspot).sharamount || sharamount->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shardamping = pedited->locallab.spots.at(pp->locallab.selspot).shardamping || shardamping->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).shariter = pedited->locallab.spots.at(pp->locallab.selspot).shariter || shariter->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sharblur = pedited->locallab.spots.at(pp->locallab.selspot).sharblur || sharblur->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensisha = pedited->locallab.spots.at(pp->locallab.selspot).sensisha || sensisha->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).inverssha = pedited->locallab.spots.at(pp->locallab.selspot).inverssha || !inverssha->get_inconsistent();
                        // Local Contrast
                        pedited->locallab.spots.at(pp->locallab.selspot).expcontrast = pedited->locallab.spots.at(pp->locallab.selspot).expcontrast || !expcontrast->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).lcradius = pedited->locallab.spots.at(pp->locallab.selspot).lcradius || lcradius->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lcamount = pedited->locallab.spots.at(pp->locallab.selspot).lcamount || lcamount->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lcdarkness = pedited->locallab.spots.at(pp->locallab.selspot).lcdarkness || lcdarkness->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lclightness = pedited->locallab.spots.at(pp->locallab.selspot).lclightness || lclightness->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).levelwav = pedited->locallab.spots.at(pp->locallab.selspot).levelwav || levelwav->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).residcont = pedited->locallab.spots.at(pp->locallab.selspot).residcont || residcont->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).residchro = pedited->locallab.spots.at(pp->locallab.selspot).residchro || residchro->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).clarilres = pedited->locallab.spots.at(pp->locallab.selspot).clarilres || clarilres->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).claricres = pedited->locallab.spots.at(pp->locallab.selspot).claricres || claricres->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).clarisoft = pedited->locallab.spots.at(pp->locallab.selspot).clarisoft || clarisoft->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensilc = pedited->locallab.spots.at(pp->locallab.selspot).sensilc || sensilc->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).fftwlc = pedited->locallab.spots.at(pp->locallab.selspot).fftwlc || !fftwlc->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).locwavcurve = pedited->locallab.spots.at(pp->locallab.selspot).locwavcurve || !wavshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).csthreshold = pedited->locallab.spots.at(pp->locallab.selspot).csthreshold || csThreshold->getEditedState();
                        // Contrast by detail levels
                        pedited->locallab.spots.at(pp->locallab.selspot).expcbdl = pedited->locallab.spots.at(pp->locallab.selspot).expcbdl || !expcbdl->get_inconsistent();

                        for (int i = 0; i < 6; i++) {
                            pedited->locallab.spots.at(pp->locallab.selspot).mult[i] = pedited->locallab.spots.at(pp->locallab.selspot).mult[i] || multiplier[i]->getEditedState();
                        }

                        pedited->locallab.spots.at(pp->locallab.selspot).chromacbdl = pedited->locallab.spots.at(pp->locallab.selspot).chromacbdl || chromacbdl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).threshold = pedited->locallab.spots.at(pp->locallab.selspot).threshold || threshold->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensicb = pedited->locallab.spots.at(pp->locallab.selspot).sensicb || sensicb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).clarityml = pedited->locallab.spots.at(pp->locallab.selspot).clarityml || clarityml->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).contresid = pedited->locallab.spots.at(pp->locallab.selspot).contresid || contresid->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).blurcbdl = pedited->locallab.spots.at(pp->locallab.selspot).blurcbdl || blurcbdl->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).softradiuscb = pedited->locallab.spots.at(pp->locallab.selspot).softradiuscb || softradiuscb->getEditedState();

                        pedited->locallab.spots.at(pp->locallab.selspot).enacbMask = pedited->locallab.spots.at(pp->locallab.selspot).enacbMask || !enacbMask->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).CCmaskcbcurve = pedited->locallab.spots.at(pp->locallab.selspot).CCmaskcbcurve || !CCmaskcbshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcbcurve = pedited->locallab.spots.at(pp->locallab.selspot).LLmaskcbcurve || !LLmaskcbshape->isUnChanged();
                        pedited->locallab.spots.at(pp->locallab.selspot).HHmaskcbcurve = pedited->locallab.spots.at(pp->locallab.selspot).HHmaskcbcurve || !HHmaskcbshape->isUnChanged();

                        pedited->locallab.spots.at(pp->locallab.selspot).blendmaskcb = pedited->locallab.spots.at(pp->locallab.selspot).blendmaskcb || blendmaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).radmaskcb = pedited->locallab.spots.at(pp->locallab.selspot).radmaskcb || radmaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).chromaskcb = pedited->locallab.spots.at(pp->locallab.selspot).chromaskcb || chromaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).gammaskcb = pedited->locallab.spots.at(pp->locallab.selspot).gammaskcb || gammaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).slomaskcb = pedited->locallab.spots.at(pp->locallab.selspot).slomaskcb || slomaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).lapmaskcb = pedited->locallab.spots.at(pp->locallab.selspot).lapmaskcb || lapmaskcb->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).Lmaskcbcurve = pedited->locallab.spots.at(pp->locallab.selspot).Lmaskcbcurve || !Lmaskcbshape->isUnChanged();

                        // Denoise
                        pedited->locallab.spots.at(pp->locallab.selspot).expdenoi = pedited->locallab.spots.at(pp->locallab.selspot).expdenoi || !expdenoi->get_inconsistent();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselumf = pedited->locallab.spots.at(pp->locallab.selspot).noiselumf || noiselumf->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselumf0 = pedited->locallab.spots.at(pp->locallab.selspot).noiselumf0 || noiselumf0->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselumf2 = pedited->locallab.spots.at(pp->locallab.selspot).noiselumf2 || noiselumf2->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselumc = pedited->locallab.spots.at(pp->locallab.selspot).noiselumc || noiselumc->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselumdetail = pedited->locallab.spots.at(pp->locallab.selspot).noiselumdetail || noiselumdetail->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noiselequal = pedited->locallab.spots.at(pp->locallab.selspot).noiselequal || noiselequal->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noisechrof = pedited->locallab.spots.at(pp->locallab.selspot).noisechrof || noisechrof->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noisechroc = pedited->locallab.spots.at(pp->locallab.selspot).noisechroc || noisechroc->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).noisechrodetail = pedited->locallab.spots.at(pp->locallab.selspot).noisechrodetail || noisechrodetail->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).adjblur = pedited->locallab.spots.at(pp->locallab.selspot).adjblur || adjblur->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).bilateral = pedited->locallab.spots.at(pp->locallab.selspot).bilateral || bilateral->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).sensiden = pedited->locallab.spots.at(pp->locallab.selspot).sensiden || sensiden->getEditedState();
                        pedited->locallab.spots.at(pp->locallab.selspot).detailthr = pedited->locallab.spots.at(pp->locallab.selspot).detailthr || detailthr->getEditedState();
                    }
                }
            }
    }

    // Update Locallab tools GUI
    disableListener();
    updateSpecificGUIState();
    enableListener();
}

void Locallab::protectskins_toggled()
{
    // printf("protectskins_toggled\n");

    if (multiImage) {
        if (protectSkins->get_inconsistent()) {
            protectSkins->set_inconsistent(false);
            pskinsconn.block(true);
            protectSkins->set_active(false);
            pskinsconn.block(false);
        }
    }

    if (getEnabled() && expvibrance->getEnabled()) {
        if (listener) {
            if (protectSkins->get_active()) {
                listener->panelChanged(EvlocallabProtectSkins, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvlocallabProtectSkins, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::avoidcolorshift_toggled()
{
    // printf("avoidcolorshift_toggled\n");

    if (multiImage) {
        if (avoidColorShift->get_inconsistent()) {
            avoidColorShift->set_inconsistent(false);
            ashiftconn.block(true);
            avoidColorShift->set_active(false);
            ashiftconn.block(false);
        }
    }

    if (getEnabled() && expvibrance->getEnabled()) {
        if (listener) {
            if (avoidColorShift->get_active()) {
                listener->panelChanged(EvlocallabAvoidColorShift, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvlocallabAvoidColorShift, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::pastsattog_toggled()
{
    // printf("pastsattog_toggled\n");

    if (multiImage) {
        if (pastSatTog->get_inconsistent()) {
            pastSatTog->set_inconsistent(false);
            pastsattogconn.block(true);
            pastSatTog->set_active(false);
            pastsattogconn.block(false);
        }
    }

    // Update Vibrance GUI according to pastsattog button state (to be compliant with updateSpecificGUIState function)
    if (multiImage && pastSatTog->get_inconsistent()) {
        psThreshold->set_sensitive(true);
        saturated->set_sensitive(true);
    } else if (pastSatTog->get_active()) {
        // Link both slider, so we set saturated and psThresholds unsensitive
        psThreshold->set_sensitive(false);
        saturated->set_sensitive(false);
        saturated->setValue(pastels->getValue()); // Pastels and Saturated are linked
    } else {
        // Separate sliders, so we set saturated and psThresholds sensitive again
        psThreshold->set_sensitive(true);
        saturated->set_sensitive(true);
    }

    if (getEnabled() && expvibrance->getEnabled()) {
        if (listener) {
            if (pastSatTog->get_active()) {
                listener->panelChanged(EvlocallabPastSatTog, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvlocallabPastSatTog, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::curveChanged(CurveEditor* ce)
{
    // Color & Light
    if (getEnabled() && expcolor->getEnabled()) {
        if (ce == llshape) {
            if (listener) {
                listener->panelChanged(Evlocallabllshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == lcshape) {
            if (listener) {
                listener->panelChanged(Evlocallablcshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == ccshape) {
            if (listener) {
                listener->panelChanged(Evlocallabccshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == clshape) {
            if (listener) {
                listener->panelChanged(Evlocallabclshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == rgbshape) {
            if (listener) {
                listener->panelChanged(Evlocallabrgbshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == CCmaskshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHhmaskshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHhmaskshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskcolshapewav) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskcolshapewav, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    // Exposure
    if (getEnabled() && expexpose->getEnabled()) {
        if (ce == shapeexpos) {
            if (listener) {
                listener->panelChanged(Evlocallabshapeexpos, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == CCmaskexpshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskexpshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskexpshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskexpshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskexpshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskexpshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskexpshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskexpshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    //Shadows Highlight
    if (getEnabled() && expshadhigh->getEnabled()) {

        if (ce == CCmaskSHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskSHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskSHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskSHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskSHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskSHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LmaskSHshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskSHshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }


    //CBDL
    if (getEnabled() && expcbdl->getEnabled()) {

        if (ce == CCmaskcbshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskcbshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskcbshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskcbshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskcbshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskcbshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskcbshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskcbshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    // Vibrance
    if (getEnabled() && expvibrance->getEnabled()) {
        if (ce == skinTonesCurve) {
            if (listener) {
                listener->panelChanged(EvlocallabSkinTonesCurve, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == CCmaskvibshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskvibshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskvibshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskvibshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskvibshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskvibshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskvibshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskvibshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    //Blur
    if (getEnabled() && (expblur->getEnabled() || expdenoi->getEnabled())) {

        if (ce == CCmaskblshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskblshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskblshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskblshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskblshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskblshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskblshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskblshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskblshapewav) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskblshapewav, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    // TM
    if (getEnabled() && exptonemap->getEnabled()) {

        if (ce == CCmasktmshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmasktmshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmasktmshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmasktmshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmasktmshape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmasktmshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmasktmshape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmasktmshape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    // Retinex
    if (getEnabled() && expreti->getEnabled()) {
        if (ce == cTgainshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCTgainCurve, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == cTtransshape) {
            if (listener) {
                listener->panelChanged(EvlocallabCTtransCurve, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == CCmaskretishape) {
            if (listener) {
                listener->panelChanged(EvlocallabCCmaskretishape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == LLmaskretishape) {
            if (listener) {
                listener->panelChanged(EvlocallabLLmaskretishape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == HHmaskretishape) {
            if (listener) {
                listener->panelChanged(EvlocallabHHmaskretishape, M("HISTORY_CUSTOMCURVE"));
            }
        }

        if (ce == Lmaskretishape) {
            if (listener) {
                listener->panelChanged(EvlocallabLmaskretishape, M("HISTORY_CUSTOMCURVE"));
            }
        }

    }

    // Local contrast
    if (getEnabled() && expcontrast->getEnabled()) {
        if (ce == wavshape) {
            if (listener) {
                listener->panelChanged(EvlocallabwavCurve, M("HISTORY_CUSTOMCURVE"));
            }
        }
    }

}

void Locallab::localcontMethodChanged()
{
    if (localcontMethod->get_active_row_number() == 0) {
        levelwav->hide();
        residcont->hide();
        residchro->hide();
        clarilres->hide();
        claricres->hide();
        clarisoft->hide();
        clariFrame->hide();
        csThreshold->hide();
        lcradius->show();
        lcamount->show();
        lcdarkness->show();
        lclightness->show();
        LocalcurveEditorwav->hide();
        fftwlc->show();
    } else if (localcontMethod->get_active_row_number() == 1) {
        levelwav->show();
        residcont->show();
        residchro->show();
        clarilres->show();
        claricres->show();
        clarisoft->show();
        clariFrame->show();
        csThreshold->show();
        lcradius->hide();
        lcamount->hide();
        lcdarkness->hide();
        lclightness->hide();
        LocalcurveEditorwav->show();
        fftwlc->hide();
    }

    // printf("localcontMethodChanged\n");
    if (getEnabled() && expcontrast->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallablocalcontMethod, localcontMethod->get_active_text());
        }
    }
}


void Locallab::retinexMethodChanged()
{
    // printf("retinexMethodChanged\n");

    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabretinexMethod, retinexMethod->get_active_text());
        }
    }
}

void Locallab::softMethodChanged()
{
    // printf("softMethodChanged\n");
    if (softMethod->get_active_row_number() == 0) {
        laplace->hide();
        ctboxsoftmethod->hide();
    } else {
        laplace->show();
        ctboxsoftmethod->show();
    }

    if (getEnabled() && expsoft->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabsoftMethod, softMethod->get_active_text());
        }
    }
}
void Locallab::blMethodChanged()
{
    // printf("blMethodChanged\n");
    if (blMethod->get_active_row_number() == 0) {
        radius->show();
        fftwbl->show();
        strength->show();
        itera->hide();
        grainFrame->show();
        isogr->show();
        strengr->show();
        scalegr->show();
        medMethod->hide();
        guidbl->hide();
        epsbl->hide();
        activlum->show();
    } else if (blMethod->get_active_row_number() == 1) {
        radius->hide();
        fftwbl->hide();
        grainFrame->hide();
        isogr->hide();
        strengr->hide();
        scalegr->hide();
        strength->hide();
        itera->show();
        medMethod->show();
        guidbl->hide();
        epsbl->hide();
        activlum->show();
    } else if (blMethod->get_active_row_number() == 2) {
        radius->hide();
        fftwbl->hide();
        strength->hide();
        grainFrame->hide();
        isogr->hide();
        strengr->hide();
        scalegr->hide();
        itera->hide();
        medMethod->hide();
        guidbl->show();
        epsbl->show();
        activlum->hide();
    }

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabblMethod, blMethod->get_active_text());
        }
    }
}


void Locallab::blurMethodChanged()
{
    // printf("blurMethodChanged\n");
    if (blurMethod->get_active_row_number() == 0) {
        //    expmaskbl->show();
    } else if (blurMethod->get_active_row_number() == 1) {
        //    expmaskbl->hide();
    }

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabblurMethod, blurMethod->get_active_text());
        }
    }
}

void Locallab::medMethodChanged()
{

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabmedMethod, medMethod->get_active_text());
        }
    }
}

void Locallab::qualitycurveMethodChanged()
{
    // printf("qualitycurveMethodChanged\n");

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabqualitycurveMethod, qualitycurveMethod->get_active_text());
        }
    }
}

void Locallab::gridMethodChanged()
{
    // printf("qualitycurveMethodChanged\n");

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvLocallabgridMethod, gridMethod->get_active_text());
        }
    }
}

void Locallab::merMethodChanged()
{
    if (merMethod->get_active_row_number() == 0) {
        mask7->hide();
        conthrcol->hide();
        sensi->set_sensitive(true);
        structcol->set_sensitive(true);
        blurcolde->set_sensitive(true);
        blurcol->set_sensitive(true);
        contcol->set_sensitive(true);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(true);
        //    strcolh->set_sensitive(false);
        special->set_sensitive(true);
        invers->set_sensitive(true);
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 1) {
        mask7->hide();
        sensi->set_sensitive(true);
        structcol->set_sensitive(true);
        blurcolde->set_sensitive(true);
        blurcol->set_sensitive(true);
        contcol->set_sensitive(true);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(true);
        special->set_sensitive(true);
        invers->set_sensitive(true);
        //     strcolh->set_sensitive(false);
        conthrcol->hide();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 2) {
        mask7->show();
        sensi->set_sensitive(false);
        structcol->set_sensitive(false);
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        strcolh->set_sensitive(true);
        conthrcol->show();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 3) {
        sensi->set_sensitive(false);
        structcol->set_sensitive(false);
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        strcolh->set_sensitive(true);
        mask7->show();
        conthrcol->show();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 4) {
        mask7->show();
        conthrcol->hide();
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        sensi->set_sensitive(false);
        structcol->set_sensitive(false);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        strcolh->set_sensitive(true);
        gridmerFrame->show();
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvLocallabmerMethod, merMethod->get_active_text());
        }
    }
}

void Locallab::toneMethodChanged()
{
    // printf("qualitycurveMethodChanged\n");

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvLocallabtoneMethod, toneMethod->get_active_text());
        }
    }
}

void Locallab::mergecolMethodChanged()
{
    // printf("mergecolMethodChanged\n");

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvLocallabmergecolMethod, mergecolMethod->get_active_text());
        }
    }
}

void Locallab::showmaskcolMethodChanged()
{
    // printf("showmaskcolMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskcolMethodinv->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskexpMethodinv->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskcolMethod, "");
    }
}

void Locallab::showmaskcolMethodChangedinv()
{
    // printf("showmaskcolMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskexpMethodinv->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskcolMethodinv, "");
    }
}

void Locallab::showmaskexpMethodChangedinv()
{
    // printf("showmaskcolMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskexpMethodinv, "");
    }
}

void Locallab::showmaskexpMethodChanged()
{
    // printf("showmaskexpMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskexpMethod, "");
    }
}

void Locallab::expMethodChanged()
{
    //  printf("expMethodChanged\n");
    disableListener();

    if (expMethod->get_active_row_number() == 0) {
        pdeFrame->set_sensitive(false);
        fatFrame->set_sensitive(false);
        laplacexp->set_sensitive(false);
        balanexp->set_sensitive(false);
        linear->set_sensitive(false);
        gamm->set_sensitive(false);
        fatamount->set_sensitive(false);
        fatdetail->set_sensitive(false);
        fatanchor->set_sensitive(false);
        fatlevel->set_sensitive(false);
        softradiusexp->set_sensitive(true);
    } else if (expMethod->get_active_row_number() == 1) {
        laplacexp->set_sensitive(true);
        balanexp->set_sensitive(true);
        linear->set_sensitive(true);
        pdeFrame->set_sensitive(true);
        fatFrame->set_sensitive(true);
        gamm->set_sensitive(true);
        fatamount->set_sensitive(true);
        fatdetail->set_sensitive(true);
        fatanchor->set_sensitive(true);
        fatlevel->set_sensitive(true);
        softradiusexp->set_sensitive(false);
    }

    enableListener();

    if (getEnabled() && expexpose->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabexpMethod, expMethod->get_active_text());
        }
    }
}


void Locallab::exnoiseMethodChanged()
{

    if (getEnabled() && expexpose->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabexnoiseMethod, exnoiseMethod->get_active_text());
        }
    }
}

void Locallab::shMethodChanged()
{
    //  printf("expMethodChanged\n");
    disableListener();

    if (shMethod->get_active_row_number() == 0) {
        for (int i = 0; i < 5; i++) {
            multipliersh[i]->hide();
        }

        detailSH->hide();
        gamFrame->hide();
        highlights->show();
        h_tonalwidth->show();
        shadows->show();
        s_tonalwidth->show();
        sh_radius->show();
//        blurSHde->show();
    } else if (shMethod->get_active_row_number() == 1) {
        for (int i = 0; i < 5; i++) {
            multipliersh[i]->show();
        }

        detailSH->show();
        gamFrame->show();
        highlights->hide();
        h_tonalwidth->hide();
        shadows->hide();
        s_tonalwidth->hide();
        sh_radius->hide();
//        blurSHde->hide();

    }

    enableListener();

    if (getEnabled() && expshadhigh->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabshMethod, shMethod->get_active_text());
        }
    }
}


void Locallab::showmaskSHMethodChanged()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskSHMethod, "");
    }
}

void Locallab::showmaskvibMethodChanged()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskvibMethod, "");
    }
}

void Locallab::showmaskSHMethodChangedinv()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskSHMethodinv, "");
    }
}

void Locallab::showmaskcbMethodChanged()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    showmaskblMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskcbMethod, "");
    }
}

void Locallab::showmaskblMethodChanged()
{
    // printf("showmaskblMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskblMethod, "");
    }
}



void Locallab::showmasktmMethodChanged()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmasktmMethod, "");
    }
}


void Locallab::showmaskretiMethodChanged()
{
    // printf("showmaskSHMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskcbMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmaskretiMethod, "");
    }
}

void Locallab::showmasksoftMethodChanged()
{
    // printf("showmaskcolMethodChanged\n");

    // When one mask state is changed, other masks are deactivated
    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasktmMethod->set_active(0);
    enableListener();

    if (listener) {
        listener->panelChanged(EvlocallabshowmasksoftMethod, "");
    }
}

void Locallab::resetMaskVisibility()
{
    // printf("resetMaskVisibility\n");

    disableListener();
    showmaskexpMethodinv->set_active(0);
    showmaskcolMethodinv->set_active(0);
    showmaskcolMethod->set_active(0);
    showmaskexpMethod->set_active(0);
    showmaskSHMethod->set_active(0);
    showmaskvibMethod->set_active(0);
    showmaskSHMethodinv->set_active(0);
    showmaskcbMethod->set_active(0);
    showmaskretiMethod->set_active(0);
    showmasksoftMethod->set_active(0);
    showmasktmMethod->set_active(0);
    enableListener();
}

Locallab::llMaskVisibility* Locallab::getMaskVisibility()
{
    llMaskVisibility* maskStruct = new llMaskVisibility();
    maskStruct->colorMaskinv = showmaskcolMethodinv->get_active_row_number();
    maskStruct->colorMask = showmaskcolMethod->get_active_row_number();
    maskStruct->expMask = showmaskexpMethod->get_active_row_number();
    maskStruct->expMaskinv = showmaskexpMethodinv->get_active_row_number();
    maskStruct->SHMask = showmaskSHMethod->get_active_row_number();
    maskStruct->vibMask = showmaskvibMethod->get_active_row_number();
    maskStruct->SHMaskinv = showmaskSHMethodinv->get_active_row_number();
    maskStruct->cbMask = showmaskcbMethod->get_active_row_number();
    maskStruct->retiMask = showmaskretiMethod->get_active_row_number();
    maskStruct->softMask = showmasksoftMethod->get_active_row_number();
    maskStruct->tmMask = showmasktmMethod->get_active_row_number();
    maskStruct->blMask = showmaskblMethod->get_active_row_number();
    return maskStruct;
}

void Locallab::specialChanged()
{

    if (multiImage) {
        if (special->get_inconsistent()) {
            special->set_inconsistent(false);
            specialConn.block(true);
            special->set_active(false);
            specialConn.block(false);
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (special->get_active()) {
                listener->panelChanged(EvLocallabspecial, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabspecial, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::toolcolChanged()
{

    if (multiImage) {
        if (toolcol->get_inconsistent()) {
            toolcol->set_inconsistent(false);
            toolcolConn.block(true);
            toolcol->set_active(false);
            toolcolConn.block(false);
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (toolcol->get_active()) {
                listener->panelChanged(EvLocallabtoolcol, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabtoolcol, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enaColorMaskChanged()
{
    // printf("enaColorMaskChanged\n");

    if (multiImage) {
        if (enaColorMask->get_inconsistent()) {
            enaColorMask->set_inconsistent(false);
            enaColorMaskConn.block(true);
            enaColorMask->set_active(false);
            enaColorMaskConn.block(false);
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (enaColorMask->get_active()) {
                listener->panelChanged(EvLocallabEnaColorMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaColorMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::fftColorMaskChanged()
{
    // printf("fftColorMaskChanged\n");

    if (multiImage) {
        if (fftColorMask->get_inconsistent()) {
            fftColorMask->set_inconsistent(false);
            fftColorMaskConn.block(true);
            fftColorMask->set_active(false);
            fftColorMaskConn.block(false);
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (fftColorMask->get_active()) {
                listener->panelChanged(EvLocallabfftColorMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabfftColorMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enaExpMaskChanged()
{
    // printf("enaExpMaskChanged\n");

    if (multiImage) {
        if (enaExpMask->get_inconsistent()) {
            enaExpMask->set_inconsistent(false);
            enaExpMaskConn.block(true);
            enaExpMask->set_active(false);
            enaExpMaskConn.block(false);
        }
    }

    if (getEnabled() && expexpose->getEnabled()) {
        if (listener) {
            if (enaExpMask->get_active()) {
                listener->panelChanged(EvLocallabEnaExpMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaExpMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enaExpMaskaftChanged()
{
    // printf("enaExpMaskChanged\n");

    if (multiImage) {
        if (enaExpMaskaft->get_inconsistent()) {
            enaExpMaskaft->set_inconsistent(false);
            enaExpMaskaftConn.block(true);
            enaExpMaskaft->set_active(false);
            enaExpMaskaftConn.block(false);
        }
    }

    if (getEnabled() && expexpose->getEnabled()) {
        if (listener) {
            if (enaExpMaskaft->get_active()) {
                listener->panelChanged(EvLocallabEnaExpMaskaft, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaExpMaskaft, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enaSHMaskChanged()
{
    // printf("enaSHMaskChanged\n");

    if (multiImage) {
        if (enaSHMask->get_inconsistent()) {
            enaSHMask->set_inconsistent(false);
            enaSHMaskConn.block(true);
            enaSHMask->set_active(false);
            enaSHMaskConn.block(false);
        }
    }

    if (getEnabled() && expshadhigh->getEnabled()) {
        if (listener) {
            if (enaSHMask->get_active()) {
                listener->panelChanged(EvLocallabEnaSHMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaSHMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enavibMaskChanged()
{
    // printf("enavibMaskChanged\n");

    if (multiImage) {
        if (enavibMask->get_inconsistent()) {
            enavibMask->set_inconsistent(false);
            enavibMaskConn.block(true);
            enavibMask->set_active(false);
            enavibMaskConn.block(false);
        }
    }

    if (getEnabled() && expvibrance->getEnabled()) {
        if (listener) {
            if (enavibMask->get_active()) {
                listener->panelChanged(EvLocallabEnavibMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnavibMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enablMaskChanged()
{
    // printf("enablmaskChanged\n");

    if (multiImage) {
        if (enablMask->get_inconsistent()) {
            enablMask->set_inconsistent(false);
            enablMaskConn.block(true);
            enablMask->set_active(false);
            enablMaskConn.block(false);
        }
    }

    if (getEnabled() && (expblur->getEnabled() || expdenoi->getEnabled())) {
        if (listener) {
            if (enablMask->get_active()) {
                listener->panelChanged(EvLocallabEnablMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnablMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::fftwblChanged()
{
    // printf("fftwblChanged\n");

    if (multiImage) {
        if (fftwbl->get_inconsistent()) {
            fftwbl->set_inconsistent(false);
            fftwblConn.block(true);
            fftwbl->set_active(false);
            fftwblConn.block(false);
        }
    }

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            if (fftwbl->get_active()) {
                listener->panelChanged(Evlocallabfftwbl, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabfftwbl, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::toolblChanged()
{
    // printf("fftwblChanged\n");

    if (multiImage) {
        if (toolbl->get_inconsistent()) {
            toolbl->set_inconsistent(false);
            toolblConn.block(true);
            toolbl->set_active(false);
            toolblConn.block(false);
        }
    }

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            if (toolbl->get_active()) {
                listener->panelChanged(Evlocallabtoolbl, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabtoolbl, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::enatmMaskChanged()
{
    printf("enatmmaskChanged\n");

    if (multiImage) {
        if (enatmMask->get_inconsistent()) {
            enatmMask->set_inconsistent(false);
            enatmMaskConn.block(true);
            enatmMask->set_active(false);
            enatmMaskConn.block(false);
        }
    }

    if (getEnabled() && exptonemap->getEnabled()) {
        if (listener) {
            if (enatmMask->get_active()) {
                listener->panelChanged(EvLocallabEnatmMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnatmMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enatmMaskaftChanged()
{
    printf("enatmmaskaftChanged\n");

    if (multiImage) {
        if (enatmMaskaft->get_inconsistent()) {
            enatmMaskaft->set_inconsistent(false);
            enatmMaskaftConn.block(true);
            enatmMaskaft->set_active(false);
            enatmMaskaftConn.block(false);
        }
    }

    if (getEnabled() && exptonemap->getEnabled()) {
        if (listener) {
            if (enatmMaskaft->get_active()) {
                listener->panelChanged(EvLocallabEnatmMaskaft, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnatmMaskaft, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::enaretiMaskChanged()
{
    printf("enaretimaskChanged\n");

    if (multiImage) {
        if (enaretiMask->get_inconsistent()) {
            enaretiMask->set_inconsistent(false);
            enaretiMaskConn.block(true);
            enaretiMask->set_active(false);
            enaretiMaskConn.block(false);
        }
    }

    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (enaretiMask->get_active()) {
                listener->panelChanged(EvLocallabEnaretiMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaretiMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enaretiMasktmapChanged()
{
    // printf("enacbMaskChanged\n");

    if (multiImage) {
        if (enaretiMasktmap->get_inconsistent()) {
            enaretiMasktmap->set_inconsistent(false);
            enaretiMasktmapConn.block(true);
            enaretiMasktmap->set_active(false);
            enaretiMasktmapConn.block(false);
        }
    }

    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (enaretiMasktmap->get_active()) {
                listener->panelChanged(EvLocallabEnaretiMasktmap, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnaretiMasktmap, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::enacbMaskChanged()
{
    // printf("enacbMaskChanged\n");

    if (multiImage) {
        if (enacbMask->get_inconsistent()) {
            enacbMask->set_inconsistent(false);
            enacbMaskConn.block(true);
            enacbMask->set_active(false);
            enacbMaskConn.block(false);
        }
    }

    if (getEnabled() && expcbdl->getEnabled()) {
        if (listener) {
            if (enacbMask->get_active()) {
                listener->panelChanged(EvLocallabEnacbMask, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(EvLocallabEnacbMask, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::inversChanged()
{
    // printf("inversChanged\n");

    if (multiImage) {
        if (invers->get_inconsistent()) {
            invers->set_inconsistent(false);
            inversConn.block(true);
            invers->set_active(false);
            inversConn.block(false);
        }
    }

    // Update Color & Light GUI according to invers button state (to be compliant with updateSpecificGUIState function)
    if (multiImage && invers->get_inconsistent()) {
        sensi->show();
        llCurveEditorG->show();
        clCurveEditorG->show();
        HCurveEditorG->show();
        curvactiv->hide();
        qualitycurveMethod->show();
        labqualcurv->show();
        expmaskcol->show();
        expmaskcol1->show();
        structcol->show();
        strengthgrid->hide();
        blurcolde->show();
        blurcol->show();
        contcol->show();
        softradiuscol->show();
        showmaskcolMethod->show(); // Being able to change Color & Light mask visibility is useless in batch mode
        gridFrame->hide();
        showmaskcolMethodinv->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        merge1colFrame->show();

    } else if (invers->get_active()) {
        sensi->show();
        llCurveEditorG->show();
        clCurveEditorG->hide();
        HCurveEditorG->hide();
        curvactiv->hide();
        qualitycurveMethod->hide();
        labqualcurv->hide();
        expmaskcol->show();
        expmaskcol1->hide();
        structcol->hide();
        blurcolde->show();
        blurcol->hide();
        contcol->hide();
        gridFrame->hide();
        strengthgrid->hide();
        softradiuscol->hide();
        showmaskcolMethod->hide();
        showmaskcolMethodinv->show();
        merge1colFrame->hide();
    } else {
        sensi->show();
        llCurveEditorG->show();
        clCurveEditorG->show();
        HCurveEditorG->show();
        curvactiv->hide();
        qualitycurveMethod->show();
        labqualcurv->show();
        expmaskcol->show();
        expmaskcol1->show();
        structcol->show();
        blurcolde->show();
        blurcol->show();
        contcol->show();
        gridFrame->show();
        softradiuscol->show();
        showmaskcolMethodinv->hide();
        showmaskcolMethod->show();
        merge1colFrame->show();

        if (batchMode) {
            showmaskcolMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
            showmaskcolMethodinv->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (invers->get_active()) {
                listener->panelChanged(Evlocallabinvers, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabinvers, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::inversexChanged()
{
    // printf("inversChanged\n");

    if (multiImage) {
        if (inversex->get_inconsistent()) {
            inversex->set_inconsistent(false);
            inversexConn.block(true);
            inversex->set_active(false);
            inversexConn.block(false);
        }
    }

    // Update Color & Light GUI according to invers button state (to be compliant with updateSpecificGUIState function)
    if (multiImage && inversex->get_inconsistent()) {
        sensiex->show();
        curveEditorG->show();
        expmaskexp->show();
        structexp->show();
        blurexpde->show();
        shadex->show();
        expMethod->show();
        exnoiseMethod->show();
        gradFrame->show();

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            softradiusexp->show();
            fatFrame->hide();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->show();
            fatFrame->show();
            softradiusexp->hide();
        }

        showmaskexpMethodinv->hide(); // Being able to change Color & Light mask visibility is useless in batch mode

        softradiusexp->show();
        showmaskexpMethod->show(); // Being able to change Color & Light mask visibility is useless in batch mode
    } else if (inversex->get_active()) {
        sensiex->show();
        curveEditorG->show();
        expmaskexp->show();
        structexp->hide();
        shadex->hide();
        blurexpde->show();
        softradiusexp->hide();
        expMethod->hide();
        exnoiseMethod->hide();
        showmaskexpMethod->hide();
        showmaskexpMethodinv->show();
        expMethod->set_active(0);
        gradFrame->hide();

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->show();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->hide();
        }

    } else {
        sensiex->show();
        curveEditorG->show();
        expmaskexp->show();
        structexp->show();
        blurexpde->show();
        softradiusexp->show();
        shadex->show();
        expMethod->show();
        exnoiseMethod->show();
        gradFrame->show();

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->show();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->show();
            fatFrame->show();
            softradiusexp->hide();
        }

        showmaskexpMethodinv->show();
        showmaskexpMethod->hide();

        if (batchMode) {
            showmaskexpMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
            showmaskexpMethodinv->hide();
        }
    }

    if (getEnabled() && expexpose->getEnabled()) {
        if (listener) {
            if (inversex->get_active()) {
                listener->panelChanged(Evlocallabinversex, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabinversex, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::inversshChanged()
{
    // printf("inversChanged\n");

    if (multiImage) {
        if (inverssh->get_inconsistent()) {
            inverssh->set_inconsistent(false);
            inversshConn.block(true);
            inverssh->set_active(false);
            inversshConn.block(false);
        }
    }

    // Update Color & Light GUI according to invers button state (to be compliant with updateSpecificGUIState function)
    if (multiImage && inverssh->get_inconsistent()) {

        sensihs->show();
        blurSHde->show();
        expmasksh->show();
        showmaskSHMethod->show();
        showmaskSHMethodinv->hide();
        gradSHFrame->show();

        showmaskSHMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
    } else if (inverssh->get_active()) {
        //  printf("Inv SH\n");
        sensihs->show();
        expmasksh->show();
        blurSHde->show();
        showmaskSHMethod->hide();
        showmaskSHMethodinv->show();
//        shMethod->set_active(0);
        gradSHFrame->hide();

    } else {
        //   printf("Pas Inv SH\n");
        sensihs->show();
        expmasksh->show();
        blurSHde->show();
        showmaskSHMethod->show();
        showmaskSHMethodinv->hide();
        gradSHFrame->show();

        if (batchMode) {
            showmaskSHMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
            showmaskSHMethodinv->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        }
    }

    if (getEnabled() && expshadhigh->getEnabled()) {
        if (listener) {
            if (inverssh->get_active()) {
                listener->panelChanged(Evlocallabinverssh, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabinverssh, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::curvactivChanged()
{
    // printf("curvactivChanged\n");

    if (multiImage) {
        if (curvactiv->get_inconsistent()) {
            curvactiv->set_inconsistent(false);
            curvactivConn.block(true);
            curvactiv->set_active(false);
            curvactivConn.block(false);
        }
    }

    if (getEnabled() && expcolor->getEnabled()) {
        if (listener) {
            if (curvactiv->get_active()) {
                listener->panelChanged(Evlocallabcurvactiv, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabcurvactiv, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::activlumChanged()
{
    // printf("activlumChanged\n");

    if (multiImage) {
        if (activlum->get_inconsistent()) {
            activlum->set_inconsistent(false);
            activlumConn.block(true);
            activlum->set_active(false);
            activlumConn.block(false);
        }
    }

    if (getEnabled() && expblur->getEnabled()) {
        if (listener) {
            if (activlum->get_active()) {
                listener->panelChanged(Evlocallabactivlum, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabactivlum, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::fftwlcChanged()
{
    // printf("fftwlcChanged\n");

    if (multiImage) {
        if (fftwlc->get_inconsistent()) {
            fftwlc->set_inconsistent(false);
            fftwlcConn.block(true);
            fftwlc->set_active(false);
            fftwlcConn.block(false);
        }
    }

    if (getEnabled() && expcontrast->getEnabled()) {
        if (listener) {
            if (fftwlc->get_active()) {
                listener->panelChanged(Evlocallabfftwlc, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabfftwlc, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::fftwretiChanged()
{
    // printf("fftwlcChanged\n");

    if (multiImage) {
        if (fftwreti->get_inconsistent()) {
            fftwreti->set_inconsistent(false);
            fftwretiConn.block(true);
            fftwreti->set_active(false);
            fftwretiConn.block(false);
        }
    }

    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (fftwreti->get_active()) {
                listener->panelChanged(Evlocallabfftwreti, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabfftwreti, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::inversshaChanged()
{
    // printf("inversshaChanged\n");

    if (multiImage) {
        if (inverssha->get_inconsistent()) {
            inverssha->set_inconsistent(false);
            inversshaConn.block(true);
            inverssha->set_active(false);
            inversshaConn.block(false);
        }
    }

    // Update Sharpening GUI according to inverssha button state (to be compliant with updateSpecificGUIState function)
    sensisha->show();

    if (getEnabled() && expsharp->getEnabled()) {
        if (listener) {
            if (inverssha->get_active()) {
                listener->panelChanged(Evlocallabinverssha, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabinverssha, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::equiltmChanged()
{

    if (multiImage) {
        if (equiltm->get_inconsistent()) {
            equiltm->set_inconsistent(false);
            equiltmConn.block(true);
            equiltm->set_active(false);
            equiltmConn.block(false);
        }
    }


    if (getEnabled() && exptonemap->getEnabled()) {
        if (listener) {
            if (equiltm->get_active()) {
                listener->panelChanged(Evlocallabequiltm, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabequiltm, M("GENERAL_DISABLED"));
            }
        }
    }
}



void Locallab::equilretChanged()
{

    if (multiImage) {
        if (equilret->get_inconsistent()) {
            equilret->set_inconsistent(false);
            equilretConn.block(true);
            equilret->set_active(false);
            equilretConn.block(false);
        }
    }


    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (equilret->get_active()) {
                listener->panelChanged(Evlocallabequilret, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabequilret, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::loglinChanged()
{

    if (multiImage) {
        if (loglin->get_inconsistent()) {
            loglin->set_inconsistent(false);
            loglinConn.block(true);
            loglin->set_active(false);
            loglinConn.block(false);
        }
    }


    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (loglin->get_active()) {
                listener->panelChanged(Evlocallabloglin, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabloglin, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::lumonlyChanged()
{

    if (multiImage) {
        if (lumonly->get_inconsistent()) {
            lumonly->set_inconsistent(false);
            lumonlyConn.block(true);
            lumonly->set_active(false);
            lumonlyConn.block(false);
        }
    }


    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (lumonly->get_active()) {
                listener->panelChanged(Evlocallablumonly, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallablumonly, M("GENERAL_DISABLED"));
            }
        }
    }
}


void Locallab::inversretChanged()
{
    // printf("inversretChanged\n");

    if (multiImage) {
        if (inversret->get_inconsistent()) {
            inversret->set_inconsistent(false);
            inversretConn.block(true);
            inversret->set_active(false);
            inversretConn.block(false);
        }
    }

    // Update Retinex GUI according to inversret button state (to be compliant with updateSpecificGUIState function)
    if (multiImage && inversret->get_inconsistent()) {
        sensih->show();
        softradiusret->show();
        expmaskreti->show();
    } else if (inversret->get_active()) {
        sensih->show();
        dehaz->show();
        depth->show();
        softradiuscol->show();
        expmaskreti->show();
    } else {
        sensih->show();
        dehaz->show();
        depth->show();
        softradiusret->show();
        expmaskreti->show();
    }

    if (getEnabled() && expreti->getEnabled()) {
        if (listener) {
            if (inversret->get_active()) {
                listener->panelChanged(Evlocallabinversret, M("GENERAL_ENABLED"));
            } else {
                listener->panelChanged(Evlocallabinversret, M("GENERAL_DISABLED"));
            }
        }
    }
}

void Locallab::setParamEditable(bool cond)
{
    // printf("setParamEditable: %d\n", cond);

    // Update params editable state for controlspotpanel
    expsettings->setParamEditable(cond);

    // Color & Light
    expcolor->set_sensitive(cond);
    // Exposure
    expexpose->set_sensitive(cond);
    // Shadow highlight
    expshadhigh->set_sensitive(cond);
    // Vibrance
    expvibrance->set_sensitive(cond);
    // Soft Light
    expsoft->set_sensitive(cond);
    // Blur & Noise
    expblur->set_sensitive(cond);
    // Tone Mapping
    exptonemap->set_sensitive(cond);
    // Retinex
    expreti->set_sensitive(cond);
    // Sharpening
    expsharp->set_sensitive(cond);
    // Local Contrast
    expcontrast->set_sensitive(cond);
    // Contrast by detail levels
    expcbdl->set_sensitive(cond);
    // Denoise
    expdenoi->set_sensitive(cond);
}

void Locallab::setDefaults(const rtengine::procparams::ProcParams* defParams, const ParamsEdited* pedited)
{
    defparams = defParams;
    defpedited = pedited;

    if (pedited) {
        pe = new ParamsEdited(*pedited);
    } else {
        pe = nullptr;
    }
}

void Locallab::setDefaults(const rtengine::procparams::ProcParams * defParams, const ParamsEdited * pedited, int id)
{
    // printf("setDefaults\n");

    // Set default values and edited states for controlspotpanel
    expsettings->setDefaults(defParams, pedited, id);

    // Find vector index of given spot id (index = -1 if not found)
    int index = -1;

    for (int i = 0; i < (int)defParams->locallab.spots.size(); i++) {
        if (defParams->locallab.spots.at(i).id == id) {
            index = i;

            break;
        }
    }

    // Set default values for adjusters
    const rtengine::procparams::LocallabParams::LocallabSpot* defSpot = new rtengine::procparams::LocallabParams::LocallabSpot();

    if (index != -1 && index < (int)defParams->locallab.spots.size()) {
        defSpot = &defParams->locallab.spots.at(index);
    }

    // Color & Light
    lightness->setDefault((double)defSpot->lightness);
    contrast->setDefault((double)defSpot->contrast);
    chroma->setDefault((double)defSpot->chroma);
    labgrid->setDefault(defSpot->labgridALow / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridBLow / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridAHigh / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridBHigh / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX);
    labgridmerg->setDefault(defSpot->labgridALowmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridBLowmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridAHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, defSpot->labgridBHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX);
    sensi->setDefault((double)defSpot->sensi);
    structcol->setDefault((double)defSpot->structcol);
    strcol->setDefault((double)defSpot->strcol);
    strcolab->setDefault((double)defSpot->strcolab);
    strcolh->setDefault((double)defSpot->strcolh);
    angcol->setDefault((double)defSpot->angcol);
    blurcolde->setDefault((double)defSpot->blurcolde);
    blurcol->setDefault((double)defSpot->blurcol);
    contcol->setDefault((double)defSpot->contcol);
    blendmaskcol->setDefault((double)defSpot->blendmaskcol);
    radmaskcol->setDefault(defSpot->radmaskcol);
    chromaskcol->setDefault(defSpot->chromaskcol);
    gammaskcol->setDefault(defSpot->gammaskcol);
    slomaskcol->setDefault(defSpot->slomaskcol);
    shadmaskcol->setDefault(defSpot->shadmaskcol);
    strumaskcol->setDefault(defSpot->strumaskcol);
    lapmaskcol->setDefault(defSpot->lapmaskcol);
    softradiuscol->setDefault(defSpot->softradiuscol);
    opacol->setDefault(defSpot->opacol);
    mercol->setDefault(defSpot->mercol);
    merlucol->setDefault(defSpot->merlucol);
    conthrcol->setDefault(defSpot->conthrcol);
    csThresholdcol->setDefault<int>(defSpot->csthresholdcol);
    // Exposure
    expcomp->setDefault(defSpot->expcomp);
    hlcompr->setDefault((double)defSpot->hlcompr);
    hlcomprthresh->setDefault((double)defSpot->hlcomprthresh);
    black->setDefault((double)defSpot->black);
    shadex->setDefault((double)defSpot->shadex);
    shcompr->setDefault((double)defSpot->shcompr);
    expchroma->setDefault((double)defSpot->expchroma);
    warm->setDefault((double)defSpot->warm);
    sensiex->setDefault((double)defSpot->sensiex);
    structexp->setDefault((double)defSpot->structexp);
    blurexpde->setDefault((double)defSpot->blurexpde);
    strexp->setDefault((double)defSpot->strexp);
    angexp->setDefault((double)defSpot->angexp);
    blendmaskexp->setDefault((double)defSpot->blendmaskexp);
    radmaskexp->setDefault(defSpot->radmaskexp);
    chromaskexp->setDefault(defSpot->chromaskexp);
    gammaskexp->setDefault(defSpot->gammaskexp);
    slomaskexp->setDefault(defSpot->slomaskexp);
    lapmaskexp->setDefault(defSpot->lapmaskexp);
    strmaskexp->setDefault(defSpot->strmaskexp);
    angmaskexp->setDefault(defSpot->angmaskexp);
    softradiusexp->setDefault(defSpot->softradiusexp);
    laplacexp->setDefault(defSpot->laplacexp);
    balanexp->setDefault(defSpot->balanexp);
    linear->setDefault(defSpot->linear);
    gamm->setDefault(defSpot->gamm);
    fatamount->setDefault(defSpot->fatamount);
    fatdetail->setDefault(defSpot->fatdetail);
    fatanchor->setDefault(defSpot->fatanchor);
    fatlevel->setDefault(defSpot->fatlevel);
    // Shadow highlight
    highlights->setDefault((double)defSpot->highlights);
    h_tonalwidth->setDefault((double)defSpot->h_tonalwidth);
    shadows->setDefault((double)defSpot->shadows);
    s_tonalwidth->setDefault((double)defSpot->s_tonalwidth);
    sh_radius->setDefault((double)defSpot->sh_radius);
    sensihs->setDefault((double)defSpot->sensihs);
    blendmaskSH->setDefault((double)defSpot->blendmaskSH);
    radmaskSH->setDefault(defSpot->radmaskSH);
    blurSHde->setDefault((double)defSpot->blurSHde);
    strSH->setDefault((double)defSpot->strSH);
    angSH->setDefault((double)defSpot->angSH);
    chromaskSH->setDefault(defSpot->chromaskSH);
    gammaskSH->setDefault(defSpot->gammaskSH);
    slomaskSH->setDefault(defSpot->slomaskSH);
    lapmaskSH->setDefault(defSpot->lapmaskSH);
    detailSH->setDefault(defSpot->detailSH);

    for (int i = 0; i < 5; i++) {
        multipliersh[i]->setDefault(defSpot->multsh[i]);
    }

    fatamountSH->setDefault(defSpot->fatamountSH);
    fatanchorSH->setDefault(defSpot->fatanchorSH);
    gamSH->setDefault(defSpot->gamSH);
    sloSH->setDefault(defSpot->sloSH);

    // Vibrance
    saturated->setDefault((double)defSpot->saturated);
    pastels->setDefault((double)defSpot->pastels);
    psThreshold->setDefault<int>(defSpot->psthreshold);
    sensiv->setDefault((double)defSpot->sensiv);
    blendmaskvib->setDefault((double)defSpot->blendmaskvib);
    radmaskvib->setDefault(defSpot->radmaskvib);
    chromaskvib->setDefault(defSpot->chromaskvib);
    gammaskvib->setDefault(defSpot->gammaskvib);
    slomaskvib->setDefault(defSpot->slomaskvib);
    lapmaskvib->setDefault(defSpot->lapmaskvib);
    strvib->setDefault(defSpot->strvib);
    strvibab->setDefault(defSpot->strvibab);
    strvibh->setDefault(defSpot->strvibh);
    angvib->setDefault(defSpot->angvib);
    // Soft Light
    streng->setDefault((double)defSpot->streng);
    sensisf->setDefault((double)defSpot->sensisf);
    laplace->setDefault(defSpot->laplace);
    // Blur & Noise
    radius->setDefault(defSpot->radius);
    strength->setDefault((double)defSpot->strength);
    sensibn->setDefault((double)defSpot->sensibn);
    itera->setDefault((double)defSpot->itera);
    isogr->setDefault((double)defSpot->isogr);
    strengr->setDefault((double)defSpot->strengr);
    scalegr->setDefault((double)defSpot->scalegr);
    guidbl->setDefault((double)defSpot->guidbl);
    epsbl->setDefault((double)defSpot->epsbl);
    blendmaskbl->setDefault((double)defSpot->blendmaskbl);
    radmaskbl->setDefault(defSpot->radmaskbl);
    chromaskbl->setDefault(defSpot->chromaskbl);
    gammaskbl->setDefault(defSpot->gammaskbl);
    slomaskbl->setDefault(defSpot->slomaskbl);
    lapmaskbl->setDefault(defSpot->lapmaskbl);
    shadmaskbl->setDefault(defSpot->shadmaskbl);
    strumaskbl->setDefault(defSpot->strumaskbl);
    csThresholdblur->setDefault<int>(defSpot->csthresholdblur);
    // Tone Mapping
    stren->setDefault(defSpot->stren);
    gamma->setDefault(defSpot->gamma);
    estop->setDefault(defSpot->estop);
    scaltm->setDefault(defSpot->scaltm);
    rewei->setDefault((double)defSpot->rewei);
    satur->setDefault(defSpot->satur);
    sensitm->setDefault((double)defSpot->sensitm);
    softradiustm->setDefault(defSpot->softradiustm);
    amount->setDefault(defSpot->amount);
    blendmasktm->setDefault((double)defSpot->blendmasktm);
    radmasktm->setDefault(defSpot->radmasktm);
    chromasktm->setDefault(defSpot->chromasktm);
    gammasktm->setDefault(defSpot->gammasktm);
    slomasktm->setDefault(defSpot->slomasktm);
    lapmasktm->setDefault(defSpot->lapmasktm);
    // Retinex
    str->setDefault(defSpot->str);
    chrrt->setDefault(defSpot->chrrt);
    neigh->setDefault(defSpot->neigh);
    vart->setDefault(defSpot->vart);
    offs->setDefault(defSpot->offs);
    dehaz->setDefault((double)defSpot->dehaz);
    depth->setDefault((double)defSpot->depth);
    sensih->setDefault((double)defSpot->sensih);
    softradiusret->setDefault(defSpot->softradiusret);
    blendmaskreti->setDefault((double)defSpot->blendmaskreti);
    radmaskreti->setDefault(defSpot->radmaskreti);
    chromaskreti->setDefault(defSpot->chromaskreti);
    gammaskreti->setDefault(defSpot->gammaskreti);
    slomaskreti->setDefault(defSpot->slomaskreti);
    lapmaskreti->setDefault(defSpot->lapmaskreti);
    scalereti->setDefault(defSpot->scalereti);
    darkness->setDefault(defSpot->darkness);
    lightnessreti->setDefault(defSpot->lightnessreti);
    limd->setDefault(defSpot->limd);
    cliptm->setDefault(defSpot->cliptm);
    // Sharpening
    sharcontrast->setDefault((double)defSpot->sharcontrast);
    sharradius->setDefault(defSpot->sharradius);
    sharamount->setDefault((double)defSpot->sharamount);
    shardamping->setDefault((double)defSpot->shardamping);
    shariter->setDefault((double)defSpot->shariter);
    sharblur->setDefault(defSpot->sharblur);
    sensisha->setDefault((double)defSpot->sensisha);
    // Local Contrast
    lcradius->setDefault((double)defSpot->lcradius);
    lcamount->setDefault(defSpot->lcamount);
    lcdarkness->setDefault(defSpot->lcdarkness);
    lclightness->setDefault(defSpot->lclightness);
    levelwav->setDefault(defSpot->levelwav);
    residcont->setDefault(defSpot->residcont);
    residchro->setDefault(defSpot->residchro);
    clarilres->setDefault(defSpot->clarilres);
    claricres->setDefault(defSpot->claricres);
    clarisoft->setDefault(defSpot->clarisoft);
    sensilc->setDefault((double)defSpot->sensilc);
    csThreshold->setDefault<int>(defSpot->csthreshold);

    // Contrast by detail levels
    for (int i = 0; i < 6; i++) {
        multiplier[i]->setDefault(defSpot->mult[i]);
    }

    chromacbdl->setDefault(defSpot->chromacbdl);
    threshold->setDefault(defSpot->threshold);
    sensicb->setDefault((double)defSpot->sensicb);
    clarityml->setDefault(defSpot->clarityml);
    contresid->setDefault((double)defSpot->contresid);
    blurcbdl->setDefault(defSpot->blurcbdl);
    softradiuscb->setDefault(defSpot->softradiuscb);
    blendmaskcb->setDefault((double)defSpot->blendmaskcb);
    radmaskcb->setDefault(defSpot->radmaskcb);
    chromaskcb->setDefault(defSpot->chromaskcb);
    gammaskcb->setDefault(defSpot->gammaskcb);
    slomaskcb->setDefault(defSpot->slomaskcb);
    lapmaskcb->setDefault(defSpot->lapmaskcb);

    // Denoise
    noiselumf->setDefault((double)defSpot->noiselumf);
    noiselumf0->setDefault((double)defSpot->noiselumf0);
    noiselumf2->setDefault((double)defSpot->noiselumf2);
    noiselumc->setDefault((double)defSpot->noiselumc);
    noiselumdetail->setDefault((double)defSpot->noiselumdetail);
    noiselequal->setDefault((double)defSpot->noiselequal);
    noisechrof->setDefault((double)defSpot->noisechrof);
    noisechroc->setDefault((double)defSpot->noisechroc);
    noisechrodetail->setDefault((double)defSpot->noisechrodetail);
    adjblur->setDefault((double)defSpot->adjblur);
    bilateral->setDefault((double)defSpot->bilateral);
    sensiden->setDefault((double)defSpot->sensiden);
    detailthr->setDefault((double)defSpot->detailthr);

    // Set default edited states for adjusters and threshold adjusters
    if (!pedited) {
        // Color & Light
        lightness->setDefaultEditedState(Irrelevant);
        contrast->setDefaultEditedState(Irrelevant);
        chroma->setDefaultEditedState(Irrelevant);
        labgrid->setEdited(Edited);
        labgridmerg->setEdited(Edited);
        sensi->setDefaultEditedState(Irrelevant);
        structcol->setDefaultEditedState(Irrelevant);
        strcol->setDefaultEditedState(Irrelevant);
        strcolab->setDefaultEditedState(Irrelevant);
        strcolh->setDefaultEditedState(Irrelevant);
        angcol->setDefaultEditedState(Irrelevant);
        strengthgrid->setDefault((double)defSpot->strengthgrid);
        blurcolde->setDefaultEditedState(Irrelevant);
        blurcol->setDefaultEditedState(Irrelevant);
        contcol->setDefaultEditedState(Irrelevant);
        blendmaskcol->setDefaultEditedState(Irrelevant);
        radmaskcol->setDefaultEditedState(Irrelevant);
        chromaskcol->setDefaultEditedState(Irrelevant);
        gammaskcol->setDefaultEditedState(Irrelevant);
        slomaskcol->setDefaultEditedState(Irrelevant);
        shadmaskcol->setDefaultEditedState(Irrelevant);
        strumaskcol->setDefaultEditedState(Irrelevant);
        lapmaskcol->setDefaultEditedState(Irrelevant);
        softradiuscol->setDefaultEditedState(Irrelevant);
        opacol->setDefaultEditedState(Irrelevant);
        mercol->setDefaultEditedState(Irrelevant);
        merlucol->setDefaultEditedState(Irrelevant);
        conthrcol->setDefaultEditedState(Irrelevant);
        csThresholdcol->setDefaultEditedState(Irrelevant);
        // Exposure
        expcomp->setDefaultEditedState(Irrelevant);
        hlcompr->setDefaultEditedState(Irrelevant);
        hlcomprthresh->setDefaultEditedState(Irrelevant);
        black->setDefaultEditedState(Irrelevant);
        shadex->setDefaultEditedState(Irrelevant);
        shcompr->setDefaultEditedState(Irrelevant);
        expchroma->setDefaultEditedState(Irrelevant);
        warm->setDefaultEditedState(Irrelevant);
        sensiex->setDefaultEditedState(Irrelevant);
        structexp->setDefaultEditedState(Irrelevant);
        blurexpde->setDefaultEditedState(Irrelevant);
        strexp->setDefaultEditedState(Irrelevant);
        angexp->setDefaultEditedState(Irrelevant);
        blendmaskexp->setDefaultEditedState(Irrelevant);
        radmaskexp->setDefaultEditedState(Irrelevant);
        chromaskexp->setDefaultEditedState(Irrelevant);
        gammaskexp->setDefaultEditedState(Irrelevant);
        slomaskexp->setDefaultEditedState(Irrelevant);
        lapmaskexp->setDefaultEditedState(Irrelevant);
        strmaskexp->setDefaultEditedState(Irrelevant);
        angmaskexp->setDefaultEditedState(Irrelevant);
        softradiusexp->setDefaultEditedState(Irrelevant);
        laplacexp->setDefaultEditedState(Irrelevant);
        balanexp->setDefaultEditedState(Irrelevant);
        linear->setDefaultEditedState(Irrelevant);
        gamm->setDefaultEditedState(Irrelevant);
        fatamount->setDefaultEditedState(Irrelevant);
        fatdetail->setDefaultEditedState(Irrelevant);
        fatanchor->setDefaultEditedState(Irrelevant);
        fatlevel->setDefaultEditedState(Irrelevant);
        // Shadow highlight
        highlights->setDefaultEditedState(Irrelevant);
        h_tonalwidth->setDefaultEditedState(Irrelevant);
        shadows->setDefaultEditedState(Irrelevant);
        s_tonalwidth->setDefaultEditedState(Irrelevant);
        sh_radius->setDefaultEditedState(Irrelevant);
        sensihs->setDefaultEditedState(Irrelevant);
        blendmaskSH->setDefaultEditedState(Irrelevant);
        radmaskSH->setDefaultEditedState(Irrelevant);
        blurSHde->setDefaultEditedState(Irrelevant);
        strSH->setDefaultEditedState(Irrelevant);
        angSH->setDefaultEditedState(Irrelevant);
        chromaskSH->setDefaultEditedState(Irrelevant);
        gammaskSH->setDefaultEditedState(Irrelevant);
        slomaskSH->setDefaultEditedState(Irrelevant);
        lapmaskSH->setDefaultEditedState(Irrelevant);
        detailSH->setDefaultEditedState(Irrelevant);

        for (int i = 0; i < 5; i++) {
            multipliersh[i]->setDefaultEditedState(Irrelevant);
        }

        fatamountSH->setDefaultEditedState(Irrelevant);
        fatanchorSH->setDefaultEditedState(Irrelevant);
        gamSH->setDefaultEditedState(Irrelevant);
        sloSH->setDefaultEditedState(Irrelevant);

        // Vibrance
        saturated->setDefaultEditedState(Irrelevant);
        pastels->setDefaultEditedState(Irrelevant);
        psThreshold->setDefaultEditedState(Irrelevant);
        sensiv->setDefaultEditedState(Irrelevant);
        blendmaskvib->setDefaultEditedState(Irrelevant);
        radmaskvib->setDefaultEditedState(Irrelevant);
        chromaskvib->setDefaultEditedState(Irrelevant);
        gammaskvib->setDefaultEditedState(Irrelevant);
        slomaskvib->setDefaultEditedState(Irrelevant);
        lapmaskvib->setDefaultEditedState(Irrelevant);
        strvib->setDefaultEditedState(Irrelevant);
        strvibab->setDefaultEditedState(Irrelevant);
        strvibh->setDefaultEditedState(Irrelevant);
        angvib->setDefaultEditedState(Irrelevant);
        // Soft Light
        streng->setDefaultEditedState(Irrelevant);
        sensisf->setDefaultEditedState(Irrelevant);
        laplace->setDefaultEditedState(Irrelevant);
        // Blur & Noise
        radius->setDefaultEditedState(Irrelevant);
        strength->setDefaultEditedState(Irrelevant);
        sensibn->setDefaultEditedState(Irrelevant);
        itera->setDefaultEditedState(Irrelevant);
        isogr->setDefaultEditedState(Irrelevant);
        strengr->setDefaultEditedState(Irrelevant);
        scalegr->setDefaultEditedState(Irrelevant);
        guidbl->setDefaultEditedState(Irrelevant);
        epsbl->setDefaultEditedState(Irrelevant);
        blendmaskbl->setDefaultEditedState(Irrelevant);
        radmaskbl->setDefaultEditedState(Irrelevant);
        chromaskbl->setDefaultEditedState(Irrelevant);
        gammaskbl->setDefaultEditedState(Irrelevant);
        slomaskbl->setDefaultEditedState(Irrelevant);
        lapmaskbl->setDefaultEditedState(Irrelevant);
        shadmaskbl->setDefaultEditedState(Irrelevant);
        strumaskbl->setDefaultEditedState(Irrelevant);
        csThresholdblur->setDefaultEditedState(Irrelevant);
        // Tone Mapping
        stren->setDefaultEditedState(Irrelevant);
        gamma->setDefaultEditedState(Irrelevant);
        estop->setDefaultEditedState(Irrelevant);
        scaltm->setDefaultEditedState(Irrelevant);
        rewei->setDefaultEditedState(Irrelevant);
        satur->setDefaultEditedState(Irrelevant);
        sensitm->setDefaultEditedState(Irrelevant);
        softradiustm->setDefaultEditedState(Irrelevant);
        amount->setDefaultEditedState(Irrelevant);
        blendmasktm->setDefaultEditedState(Irrelevant);
        radmasktm->setDefaultEditedState(Irrelevant);
        chromasktm->setDefaultEditedState(Irrelevant);
        gammasktm->setDefaultEditedState(Irrelevant);
        slomasktm->setDefaultEditedState(Irrelevant);
        lapmasktm->setDefaultEditedState(Irrelevant);
        // Retinex
        str->setDefaultEditedState(Irrelevant);
        chrrt->setDefaultEditedState(Irrelevant);
        neigh->setDefaultEditedState(Irrelevant);
        vart->setDefaultEditedState(Irrelevant);
        offs->setDefaultEditedState(Irrelevant);
        dehaz->setDefaultEditedState(Irrelevant);
        depth->setDefaultEditedState(Irrelevant);
        sensih->setDefaultEditedState(Irrelevant);
        softradiusret->setDefaultEditedState(Irrelevant);
        blendmaskreti->setDefaultEditedState(Irrelevant);
        radmaskreti->setDefaultEditedState(Irrelevant);
        chromaskreti->setDefaultEditedState(Irrelevant);
        gammaskreti->setDefaultEditedState(Irrelevant);
        slomaskreti->setDefaultEditedState(Irrelevant);
        lapmaskreti->setDefaultEditedState(Irrelevant);
        scalereti->setDefaultEditedState(Irrelevant);
        darkness->setDefaultEditedState(Irrelevant);
        lightnessreti->setDefaultEditedState(Irrelevant);
        limd->setDefaultEditedState(Irrelevant);
        cliptm->setDefaultEditedState(Irrelevant);
        // Sharpening
        sharcontrast->setDefaultEditedState(Irrelevant);
        sharradius->setDefaultEditedState(Irrelevant);
        sharamount->setDefaultEditedState(Irrelevant);
        shardamping->setDefaultEditedState(Irrelevant);
        shariter->setDefaultEditedState(Irrelevant);
        sharblur->setDefaultEditedState(Irrelevant);
        sensisha->setDefaultEditedState(Irrelevant);
        // Local Contrast
        lcradius->setDefaultEditedState(Irrelevant);
        lcamount->setDefaultEditedState(Irrelevant);
        lcdarkness->setDefaultEditedState(Irrelevant);
        lclightness->setDefaultEditedState(Irrelevant);
        levelwav->setDefaultEditedState(Irrelevant);
        residcont->setDefaultEditedState(Irrelevant);
        residchro->setDefaultEditedState(Irrelevant);
        clarilres->setDefaultEditedState(Irrelevant);
        claricres->setDefaultEditedState(Irrelevant);
        clarisoft->setDefaultEditedState(Irrelevant);
        sensilc->setDefaultEditedState(Irrelevant);
        csThreshold->setDefaultEditedState(Irrelevant);

        // Contrast by detail levels
        for (int i = 0; i < 6; i++) {
            multiplier[i]->setDefaultEditedState(Irrelevant);
        }

        chromacbdl->setDefaultEditedState(Irrelevant);
        threshold->setDefaultEditedState(Irrelevant);
        sensicb->setDefaultEditedState(Irrelevant);
        clarityml->setDefaultEditedState(Irrelevant);
        contresid->setDefaultEditedState(Irrelevant);
        blurcbdl->setDefaultEditedState(Irrelevant);
        softradiuscb->setDefaultEditedState(Irrelevant);
        blendmaskcb->setDefaultEditedState(Irrelevant);
        radmaskcb->setDefaultEditedState(Irrelevant);
        chromaskcb->setDefaultEditedState(Irrelevant);
        gammaskcb->setDefaultEditedState(Irrelevant);
        slomaskcb->setDefaultEditedState(Irrelevant);
        lapmaskcb->setDefaultEditedState(Irrelevant);

        // Denoise
        noiselumf->setDefaultEditedState(Irrelevant);
        noiselumf0->setDefaultEditedState(Irrelevant);
        noiselumf2->setDefaultEditedState(Irrelevant);
        noiselumc->setDefaultEditedState(Irrelevant);
        noiselumdetail->setDefaultEditedState(Irrelevant);
        noiselequal->setDefaultEditedState(Irrelevant);
        noisechrof->setDefaultEditedState(Irrelevant);
        noisechroc->setDefaultEditedState(Irrelevant);
        noisechrodetail->setDefaultEditedState(Irrelevant);
        adjblur->setDefaultEditedState(Irrelevant);
        bilateral->setDefaultEditedState(Irrelevant);
        sensiden->setDefaultEditedState(Irrelevant);
        detailthr->setDefaultEditedState(Irrelevant);
    } else {
        const LocallabParamsEdited::LocallabSpotEdited* defSpotState = new LocallabParamsEdited::LocallabSpotEdited(true);

        if (index != 1 && index < (int)pedited->locallab.spots.size()) {
            defSpotState = &pedited->locallab.spots.at(index);
        }

        // Color & Light
        lightness->setDefaultEditedState(defSpotState->lightness ? Edited : UnEdited);
        contrast->setDefaultEditedState(defSpotState->contrast ? Edited : UnEdited);
        chroma->setDefaultEditedState(defSpotState->chroma ? Edited : UnEdited);
        labgrid->setEdited((defSpotState->labgridALow || defSpotState->labgridBLow || defSpotState->labgridAHigh || defSpotState->labgridBHigh) ? Edited : UnEdited);
        labgridmerg->setEdited((defSpotState->labgridALowmerg || defSpotState->labgridBLowmerg || defSpotState->labgridAHighmerg || defSpotState->labgridBHighmerg) ? Edited : UnEdited);
        sensi->setDefaultEditedState(defSpotState->sensi ? Edited : UnEdited);
        structcol->setDefaultEditedState(defSpotState->structcol ? Edited : UnEdited);
        strcol->setDefaultEditedState(defSpotState->strcol ? Edited : UnEdited);
        strcolab->setDefaultEditedState(defSpotState->strcolab ? Edited : UnEdited);
        strcolh->setDefaultEditedState(defSpotState->strcolh ? Edited : UnEdited);
        angcol->setDefaultEditedState(defSpotState->angcol ? Edited : UnEdited);
        strengthgrid->setDefaultEditedState(defSpotState->strengthgrid ? Edited : UnEdited);
        blurcolde->setDefaultEditedState(defSpotState->blurcolde ? Edited : UnEdited);
        blurcol->setDefaultEditedState(defSpotState->blurcol ? Edited : UnEdited);
        contcol->setDefaultEditedState(defSpotState->contcol ? Edited : UnEdited);
        blendmaskcol->setDefaultEditedState(defSpotState->blendmaskcol ? Edited : UnEdited);
        radmaskcol->setDefaultEditedState(defSpotState->radmaskcol ? Edited : UnEdited);
        chromaskcol->setDefaultEditedState(defSpotState->chromaskcol ? Edited : UnEdited);
        gammaskcol->setDefaultEditedState(defSpotState->gammaskcol ? Edited : UnEdited);
        slomaskcol->setDefaultEditedState(defSpotState->slomaskcol ? Edited : UnEdited);
        shadmaskcol->setDefaultEditedState(defSpotState->shadmaskcol ? Edited : UnEdited);
        strumaskcol->setDefaultEditedState(defSpotState->strumaskcol ? Edited : UnEdited);
        lapmaskcol->setDefaultEditedState(defSpotState->lapmaskcol ? Edited : UnEdited);
        softradiuscol->setDefaultEditedState(defSpotState->softradiuscol ? Edited : UnEdited);
        opacol->setDefaultEditedState(defSpotState->opacol ? Edited : UnEdited);
        mercol->setDefaultEditedState(defSpotState->mercol ? Edited : UnEdited);
        merlucol->setDefaultEditedState(defSpotState->merlucol ? Edited : UnEdited);
        conthrcol->setDefaultEditedState(defSpotState->conthrcol ? Edited : UnEdited);
        csThresholdcol->setDefaultEditedState(defSpotState->csthresholdcol ? Edited : UnEdited);
        // Exposure
        expcomp->setDefaultEditedState(defSpotState->expcomp ? Edited : UnEdited);
        hlcompr->setDefaultEditedState(defSpotState->hlcompr ? Edited : UnEdited);
        hlcomprthresh->setDefaultEditedState(defSpotState->hlcomprthresh ? Edited : UnEdited);
        black->setDefaultEditedState(defSpotState->black ? Edited : UnEdited);
        shadex->setDefaultEditedState(defSpotState->shadex ? Edited : UnEdited);
        shcompr->setDefaultEditedState(defSpotState->shcompr ? Edited : UnEdited);
        expchroma->setDefaultEditedState(defSpotState->expchroma ? Edited : UnEdited);
        warm->setDefaultEditedState(defSpotState->warm ? Edited : UnEdited);
        sensiex->setDefaultEditedState(defSpotState->sensiex ? Edited : UnEdited);
        structexp->setDefaultEditedState(defSpotState->structexp ? Edited : UnEdited);
        blurexpde->setDefaultEditedState(defSpotState->blurexpde ? Edited : UnEdited);
        strexp->setDefaultEditedState(defSpotState->strexp ? Edited : UnEdited);
        angexp->setDefaultEditedState(defSpotState->angexp ? Edited : UnEdited);
        blendmaskexp->setDefaultEditedState(defSpotState->blendmaskexp ? Edited : UnEdited);
        radmaskexp->setDefaultEditedState(defSpotState->radmaskexp ? Edited : UnEdited);
        chromaskexp->setDefaultEditedState(defSpotState->chromaskexp ? Edited : UnEdited);
        gammaskexp->setDefaultEditedState(defSpotState->gammaskexp ? Edited : UnEdited);
        slomaskexp->setDefaultEditedState(defSpotState->slomaskexp ? Edited : UnEdited);
        lapmaskexp->setDefaultEditedState(defSpotState->lapmaskexp ? Edited : UnEdited);
        strmaskexp->setDefaultEditedState(defSpotState->strmaskexp ? Edited : UnEdited);
        angmaskexp->setDefaultEditedState(defSpotState->angmaskexp ? Edited : UnEdited);
        softradiusexp->setDefaultEditedState(defSpotState->softradiusexp ? Edited : UnEdited);
        laplacexp->setDefaultEditedState(defSpotState->laplacexp ? Edited : UnEdited);
        balanexp->setDefaultEditedState(defSpotState->balanexp ? Edited : UnEdited);
        linear->setDefaultEditedState(defSpotState->linear ? Edited : UnEdited);
        gamm->setDefaultEditedState(defSpotState->gamm ? Edited : UnEdited);
        fatamount->setDefaultEditedState(defSpotState->fatamount ? Edited : UnEdited);
        fatdetail->setDefaultEditedState(defSpotState->fatdetail ? Edited : UnEdited);
        fatanchor->setDefaultEditedState(defSpotState->fatanchor ? Edited : UnEdited);
        fatlevel->setDefaultEditedState(defSpotState->fatlevel ? Edited : UnEdited);
        // Shadow highlight
        highlights->setDefaultEditedState(defSpotState->highlights ? Edited : UnEdited);
        h_tonalwidth->setDefaultEditedState(defSpotState->h_tonalwidth ? Edited : UnEdited);
        shadows->setDefaultEditedState(defSpotState->shadows ? Edited : UnEdited);
        s_tonalwidth->setDefaultEditedState(defSpotState->s_tonalwidth ? Edited : UnEdited);
        sh_radius->setDefaultEditedState(defSpotState->sh_radius ? Edited : UnEdited);
        sensihs->setDefaultEditedState(defSpotState->sensihs ? Edited : UnEdited);
        blendmaskSH->setDefaultEditedState(defSpotState->blendmaskSH ? Edited : UnEdited);
        radmaskSH->setDefaultEditedState(defSpotState->radmaskSH ? Edited : UnEdited);
        blurSHde->setDefaultEditedState(defSpotState->blurSHde ? Edited : UnEdited);
        strSH->setDefaultEditedState(defSpotState->strSH ? Edited : UnEdited);
        angSH->setDefaultEditedState(defSpotState->angSH ? Edited : UnEdited);
        chromaskSH->setDefaultEditedState(defSpotState->chromaskSH ? Edited : UnEdited);
        gammaskSH->setDefaultEditedState(defSpotState->gammaskSH ? Edited : UnEdited);
        slomaskSH->setDefaultEditedState(defSpotState->slomaskSH ? Edited : UnEdited);
        lapmaskSH->setDefaultEditedState(defSpotState->lapmaskSH ? Edited : UnEdited);
        detailSH->setDefaultEditedState(defSpotState->detailSH ? Edited : UnEdited);

        for (int i = 0; i < 5; i++) {
            multipliersh[i]->setDefaultEditedState(defSpotState->multsh[i] ? Edited : UnEdited);
        }

        fatamountSH->setDefaultEditedState(defSpotState->fatamountSH ? Edited : UnEdited);
        fatanchorSH->setDefaultEditedState(defSpotState->fatanchorSH ? Edited : UnEdited);
        gamSH->setDefaultEditedState(defSpotState->gamSH ? Edited : UnEdited);
        sloSH->setDefaultEditedState(defSpotState->sloSH ? Edited : UnEdited);

        // Vibrance
        saturated->setDefaultEditedState(defSpotState->saturated ? Edited : UnEdited);
        pastels->setDefaultEditedState(defSpotState->pastels ? Edited : UnEdited);
        psThreshold->setDefaultEditedState(defSpotState->psthreshold ? Edited : UnEdited);
        sensiv->setDefaultEditedState(defSpotState->sensiv ? Edited : UnEdited);
        blendmaskvib->setDefaultEditedState(defSpotState->blendmaskvib ? Edited : UnEdited);
        radmaskvib->setDefaultEditedState(defSpotState->radmaskvib ? Edited : UnEdited);
        chromaskvib->setDefaultEditedState(defSpotState->chromaskvib ? Edited : UnEdited);
        gammaskvib->setDefaultEditedState(defSpotState->gammaskvib ? Edited : UnEdited);
        slomaskvib->setDefaultEditedState(defSpotState->slomaskvib ? Edited : UnEdited);
        lapmaskvib->setDefaultEditedState(defSpotState->lapmaskvib ? Edited : UnEdited);
        strvib->setDefaultEditedState(defSpotState->strvib ? Edited : UnEdited);
        strvibab->setDefaultEditedState(defSpotState->strvibab ? Edited : UnEdited);
        strvibh->setDefaultEditedState(defSpotState->strvibh ? Edited : UnEdited);
        angvib->setDefaultEditedState(defSpotState->angvib ? Edited : UnEdited);
        // Soft Light
        streng->setDefaultEditedState(defSpotState->streng ? Edited : UnEdited);
        sensisf->setDefaultEditedState(defSpotState->sensisf ? Edited : UnEdited);
        laplace->setDefaultEditedState(defSpotState->laplace ? Edited : UnEdited);
        // Blur & Noise
        radius->setDefaultEditedState(defSpotState->radius ? Edited : UnEdited);
        strength->setDefaultEditedState(defSpotState->strength ? Edited : UnEdited);
        sensibn->setDefaultEditedState(defSpotState->sensibn ? Edited : UnEdited);
        itera->setDefaultEditedState(defSpotState->itera ? Edited : UnEdited);
        isogr->setDefaultEditedState(defSpotState->isogr ? Edited : UnEdited);
        strengr->setDefaultEditedState(defSpotState->strengr ? Edited : UnEdited);
        scalegr->setDefaultEditedState(defSpotState->scalegr ? Edited : UnEdited);
        guidbl->setDefaultEditedState(defSpotState->guidbl ? Edited : UnEdited);
        epsbl->setDefaultEditedState(defSpotState->epsbl ? Edited : UnEdited);
        blendmaskbl->setDefaultEditedState(defSpotState->blendmaskbl ? Edited : UnEdited);
        radmaskbl->setDefaultEditedState(defSpotState->radmaskbl ? Edited : UnEdited);
        chromaskbl->setDefaultEditedState(defSpotState->chromaskbl ? Edited : UnEdited);
        gammaskbl->setDefaultEditedState(defSpotState->gammaskbl ? Edited : UnEdited);
        slomaskbl->setDefaultEditedState(defSpotState->slomaskbl ? Edited : UnEdited);
        lapmaskbl->setDefaultEditedState(defSpotState->lapmaskbl ? Edited : UnEdited);
        shadmaskbl->setDefaultEditedState(defSpotState->shadmaskbl ? Edited : UnEdited);
        strumaskbl->setDefaultEditedState(defSpotState->strumaskbl ? Edited : UnEdited);
        csThresholdblur->setDefaultEditedState(defSpotState->csthresholdblur ? Edited : UnEdited);
        // Tone Mapping
        stren->setDefaultEditedState(defSpotState->stren ? Edited : UnEdited);
        gamma->setDefaultEditedState(defSpotState->gamma ? Edited : UnEdited);
        estop->setDefaultEditedState(defSpotState->estop ? Edited : UnEdited);
        scaltm->setDefaultEditedState(defSpotState->scaltm ? Edited : UnEdited);
        rewei->setDefaultEditedState(defSpotState->rewei ? Edited : UnEdited);
        satur->setDefaultEditedState(defSpotState->satur ? Edited : UnEdited);
        sensitm->setDefaultEditedState(defSpotState->sensitm ? Edited : UnEdited);
        softradiustm->setDefaultEditedState(defSpotState->softradiustm ? Edited : UnEdited);
        amount->setDefaultEditedState(defSpotState->amount ? Edited : UnEdited);
        blendmasktm->setDefaultEditedState(defSpotState->blendmasktm ? Edited : UnEdited);
        radmasktm->setDefaultEditedState(defSpotState->radmasktm ? Edited : UnEdited);
        chromasktm->setDefaultEditedState(defSpotState->chromasktm ? Edited : UnEdited);
        gammasktm->setDefaultEditedState(defSpotState->gammasktm ? Edited : UnEdited);
        slomasktm->setDefaultEditedState(defSpotState->slomasktm ? Edited : UnEdited);
        lapmasktm->setDefaultEditedState(defSpotState->lapmasktm ? Edited : UnEdited);
        // Retinex
        str->setDefaultEditedState(defSpotState->str ? Edited : UnEdited);
        chrrt->setDefaultEditedState(defSpotState->chrrt ? Edited : UnEdited);
        neigh->setDefaultEditedState(defSpotState->neigh ? Edited : UnEdited);
        vart->setDefaultEditedState(defSpotState->vart ? Edited : UnEdited);
        offs->setDefaultEditedState(defSpotState->offs ? Edited : UnEdited);
        dehaz->setDefaultEditedState(defSpotState->dehaz ? Edited : UnEdited);
        depth->setDefaultEditedState(defSpotState->depth ? Edited : UnEdited);
        sensih->setDefaultEditedState(defSpotState->sensih ? Edited : UnEdited);
        softradiusret->setDefaultEditedState(defSpotState->softradiusret ? Edited : UnEdited);
        blendmaskreti->setDefaultEditedState(defSpotState->blendmaskreti ? Edited : UnEdited);
        radmaskreti->setDefaultEditedState(defSpotState->radmaskreti ? Edited : UnEdited);
        chromaskreti->setDefaultEditedState(defSpotState->chromaskreti ? Edited : UnEdited);
        gammaskreti->setDefaultEditedState(defSpotState->gammaskreti ? Edited : UnEdited);
        slomaskreti->setDefaultEditedState(defSpotState->slomaskreti ? Edited : UnEdited);
        lapmaskreti->setDefaultEditedState(defSpotState->lapmaskreti ? Edited : UnEdited);
        scalereti->setDefaultEditedState(defSpotState->scalereti ? Edited : UnEdited);
        darkness->setDefaultEditedState(defSpotState->darkness ? Edited : UnEdited);
        lightnessreti->setDefaultEditedState(defSpotState->lightnessreti ? Edited : UnEdited);
        limd->setDefaultEditedState(defSpotState->limd ? Edited : UnEdited);
        cliptm->setDefaultEditedState(defSpotState->cliptm ? Edited : UnEdited);
        // Sharpening
        sharcontrast->setDefaultEditedState(defSpotState->sharcontrast ? Edited : UnEdited);
        sharradius->setDefaultEditedState(defSpotState->sharradius ? Edited : UnEdited);
        sharamount->setDefaultEditedState(defSpotState->sharamount ? Edited : UnEdited);
        shardamping->setDefaultEditedState(defSpotState->shardamping ? Edited : UnEdited);
        shariter->setDefaultEditedState(defSpotState->shariter ? Edited : UnEdited);
        sharblur->setDefaultEditedState(defSpotState->sharblur ? Edited : UnEdited);
        sensisha->setDefaultEditedState(defSpotState->sensisha ? Edited : UnEdited);
        // Local Contrast
        lcradius->setDefaultEditedState(defSpotState->lcradius ? Edited : UnEdited);
        lcamount->setDefaultEditedState(defSpotState->lcamount ? Edited : UnEdited);
        lcdarkness->setDefaultEditedState(defSpotState->lcdarkness ? Edited : UnEdited);
        lclightness->setDefaultEditedState(defSpotState->lclightness ? Edited : UnEdited);
        levelwav->setDefaultEditedState(defSpotState->levelwav ? Edited : UnEdited);
        residcont->setDefaultEditedState(defSpotState->residcont ? Edited : UnEdited);
        residchro->setDefaultEditedState(defSpotState->residchro ? Edited : UnEdited);
        clarilres->setDefaultEditedState(defSpotState->clarilres ? Edited : UnEdited);
        claricres->setDefaultEditedState(defSpotState->claricres ? Edited : UnEdited);
        clarisoft->setDefaultEditedState(defSpotState->clarisoft ? Edited : UnEdited);
        sensilc->setDefaultEditedState(defSpotState->sensilc ? Edited : UnEdited);
        csThreshold->setDefaultEditedState(defSpotState->csthreshold ? Edited : UnEdited);

        // Contrast by detail levels
        for (int i = 0; i < 6; i++) {
            multiplier[i]->setDefaultEditedState(defSpotState->mult[i] ? Edited : UnEdited);
        }

        chromacbdl->setDefaultEditedState(defSpotState->chromacbdl ? Edited : UnEdited);
        threshold->setDefaultEditedState(defSpotState->threshold ? Edited : UnEdited);
        sensicb->setDefaultEditedState(defSpotState->sensicb ? Edited : UnEdited);
        clarityml->setDefaultEditedState(defSpotState->clarityml ? Edited : UnEdited);
        contresid->setDefaultEditedState(defSpotState->contresid ? Edited : UnEdited);
        blurcbdl->setDefaultEditedState(defSpotState->blurcbdl ? Edited : UnEdited);
        softradiuscb->setDefaultEditedState(defSpotState->softradiuscb ? Edited : UnEdited);

        blendmaskcb->setDefaultEditedState(defSpotState->blendmaskcb ? Edited : UnEdited);
        radmaskcb->setDefaultEditedState(defSpotState->radmaskcb ? Edited : UnEdited);
        chromaskcb->setDefaultEditedState(defSpotState->chromaskcb ? Edited : UnEdited);
        gammaskcb->setDefaultEditedState(defSpotState->gammaskcb ? Edited : UnEdited);
        slomaskcb->setDefaultEditedState(defSpotState->slomaskcb ? Edited : UnEdited);
        lapmaskcb->setDefaultEditedState(defSpotState->lapmaskcb ? Edited : UnEdited);

        // Denoise
        noiselumf->setDefaultEditedState(defSpotState->noiselumf ? Edited : UnEdited);
        noiselumf0->setDefaultEditedState(defSpotState->noiselumf0 ? Edited : UnEdited);
        noiselumf2->setDefaultEditedState(defSpotState->noiselumf2 ? Edited : UnEdited);
        noiselumc->setDefaultEditedState(defSpotState->noiselumc ? Edited : UnEdited);
        noiselumdetail->setDefaultEditedState(defSpotState->noiselumdetail ? Edited : UnEdited);
        noiselequal->setDefaultEditedState(defSpotState->noiselequal ? Edited : UnEdited);
        noisechrof->setDefaultEditedState(defSpotState->noisechrof ? Edited : UnEdited);
        noisechroc->setDefaultEditedState(defSpotState->noisechroc ? Edited : UnEdited);
        noisechrodetail->setDefaultEditedState(defSpotState->noisechrodetail ? Edited : UnEdited);
        adjblur->setDefaultEditedState(defSpotState->adjblur ? Edited : UnEdited);
        bilateral->setDefaultEditedState(defSpotState->bilateral ? Edited : UnEdited);
        sensiden->setDefaultEditedState(defSpotState->sensiden ? Edited : UnEdited);
        detailthr->setDefaultEditedState(defSpotState->detailthr ? Edited : UnEdited);
    }
}

void Locallab::adjusterAutoToggled(Adjuster* a, bool newval)
{
    // Not used
}

void Locallab::adjusterChanged(ThresholdAdjuster* a, double newBottom, double newTop)
{
    // Not used
}
void Locallab::adjusterChanged(ThresholdAdjuster* a, double newBottomLeft, double newTopLeft, double newBottomRight, double newTopRight)
{
    // Not used
}

void Locallab::adjusterChanged(ThresholdAdjuster* a, int newBottomLeft, int newTopLeft, int newBottomRight, int newTopRight)
{
    // Not used
}


void Locallab::adjusterChanged2(ThresholdAdjuster* a, int newBottomL, int newTopL, int newBottomR, int newTopR)
{
    if (getEnabled() && expcontrast->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabcsThreshold, csThreshold->getHistoryString());
        }
    }

    if (getEnabled() && expblur->getEnabled()  && complexsoft < 1) {
        if (listener) {
            listener->panelChanged(EvlocallabcsThresholdblur, csThresholdblur->getHistoryString());
        }
    }

    if (getEnabled() && expcolor->getEnabled()  && complexsoft < 1) {
        if (listener) {
            listener->panelChanged(EvlocallabcsThresholdcol, csThresholdcol->getHistoryString());
        }
    }

}

void Locallab::adjusterChanged(ThresholdAdjuster* a, int newBottom, int newTop)
{
    // printf("adjusterChangedTS\n");

    if (getEnabled() && expvibrance->getEnabled()) {
        if (listener) {
            listener->panelChanged(EvlocallabPastSatThreshold, psThreshold->getHistoryString());
        }
    }
}

void Locallab::adjusterChanged(Adjuster * a, double newval)
{
    // printf("adjusterChanged\n");

    // Color & Light
    if (getEnabled() && expcolor->getEnabled()) {
        if (a == lightness) {
            if (listener) {
                listener->panelChanged(Evlocallablightness, lightness->getTextValue());
            }
        }

        if (a == contrast) {
            if (listener) {
                listener->panelChanged(Evlocallabcontrast, contrast->getTextValue());
            }
        }

        if (a == chroma) {
            if (listener) {
                listener->panelChanged(Evlocallabchroma, chroma->getTextValue());
            }
        }

        if (a == strengthgrid) {
            if (listener) {
                listener->panelChanged(EvLocallabLabstrengthgrid, strengthgrid->getTextValue());
            }
        }

        if (a == sensi) {
            if (listener) {
                listener->panelChanged(Evlocallabsensi, sensi->getTextValue());
            }
        }

        if (a == blurcolde) {
            if (listener) {
                listener->panelChanged(Evlocallabblurcolde, blurcolde->getTextValue());
            }
        }

        if (a == blurcol) {
            if (listener) {
                listener->panelChanged(Evlocallabblurcol, blurcol->getTextValue());
            }
        }

        if (a == structcol) {
            if (listener) {
                listener->panelChanged(Evlocallabstructcol, structcol->getTextValue());
            }
        }

        if (a == contcol) {
            if (listener) {
                listener->panelChanged(Evlocallabcontcol, contcol->getTextValue());
            }
        }

        if (a == strcol) {
            if (listener) {
                listener->panelChanged(Evlocallabstrcol, strcol->getTextValue());
            }
        }

        if (a == strcolab) {
            if (listener) {
                listener->panelChanged(Evlocallabstrcolab, strcolab->getTextValue());
            }
        }

        if (a == strcolh) {
            if (listener) {
                listener->panelChanged(Evlocallabstrcolh, strcolh->getTextValue());
            }
        }

        if (a == angcol) {
            if (listener) {
                listener->panelChanged(Evlocallabangcol, angcol->getTextValue());
            }
        }

        if (a == blendmaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskcol, blendmaskcol->getTextValue());
            }
        }

        if (a == radmaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskcol, radmaskcol->getTextValue());
            }
        }

        if (a == chromaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskcol, chromaskcol->getTextValue());
            }
        }

        if (a == gammaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskcol, gammaskcol->getTextValue());
            }
        }

        if (a == slomaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskcol, slomaskcol->getTextValue());
            }
        }

        if (a == shadmaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabshadmaskcol, shadmaskcol->getTextValue());
            }
        }

        if (a == strumaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallabstrumaskcol, strumaskcol->getTextValue());
            }
        }

        if (a == lapmaskcol) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskcol, lapmaskcol->getTextValue());
            }
        }

        if (a == softradiuscol) {
            if (listener) {
                listener->panelChanged(Evlocallabsoftradiuscol, softradiuscol->getTextValue());
            }
        }

        if (a == opacol) {
            if (listener) {
                listener->panelChanged(Evlocallabopacol, opacol->getTextValue());
            }
        }

        if (a == mercol) {
            if (listener) {
                listener->panelChanged(Evlocallabmercol, mercol->getTextValue());
            }
        }

        if (a == merlucol) {
            if (listener) {
                listener->panelChanged(Evlocallabmerlucol, merlucol->getTextValue());
            }
        }

        if (a == conthrcol) {
            if (listener) {
                listener->panelChanged(Evlocallabconthrcol, conthrcol->getTextValue());
            }
        }

    }

    // Exposure
    if (a == black) {
        // Update Exposure GUI according to black adjuster state (to be compliant with updateSpecificGUIState function)
        if (multiImage && black->getEditedState() != Edited) {
            shcompr->set_sensitive(true);
        } else {
            shcompr->set_sensitive(!((int)black->getValue() == 0)); // At black = 0, shcompr value has no effect
        }
    }

    if (a == shadex) {
        // Update Exposure GUI according to shadex adjuster state (to be compliant with updateSpecificGUIState function)
        if (multiImage && shadex->getEditedState() != Edited) {
            shcompr->set_sensitive(true);
        } else {
            shcompr->set_sensitive(!((int)shadex->getValue() == 0)); // At black = 0, shcompr value has no effect
        }
    }

    if (getEnabled() && expexpose->getEnabled()) {
        if (a == expcomp) {
            if (listener) {
                listener->panelChanged(Evlocallabexpcomp, expcomp->getTextValue());
            }
        }

        if (a == hlcompr) {
            if (listener) {
                listener->panelChanged(Evlocallabhlcompr, hlcompr->getTextValue());
            }
        }

        if (a == hlcomprthresh) {
            if (listener) {
                listener->panelChanged(Evlocallabhlcomprthresh, hlcomprthresh->getTextValue());
            }
        }

        if (a == black) {
            if (listener) {
                listener->panelChanged(Evlocallabblack, black->getTextValue());
            }
        }

        if (a == shadex) {
            if (listener) {
                listener->panelChanged(Evlocallabshadex, shadex->getTextValue());
            }
        }

        if (a == shcompr) {
            if (listener) {
                listener->panelChanged(Evlocallabshcompr, shcompr->getTextValue());
            }
        }

        if (a == expchroma) {
            if (listener) {
                listener->panelChanged(Evlocallabexpchroma, expchroma->getTextValue());
            }
        }

        if (a == warm) {
            if (listener) {
                listener->panelChanged(Evlocallabwarm, warm->getTextValue());
            }
        }

        if (a == sensiex) {
            if (listener) {
                listener->panelChanged(Evlocallabsensiex, sensiex->getTextValue());
            }
        }

        if (a == structexp) {
            if (listener) {
                listener->panelChanged(Evlocallabstructexp, structexp->getTextValue());
            }
        }

        if (a == blurexpde) {
            if (listener) {
                listener->panelChanged(Evlocallabblurexpde, blurexpde->getTextValue());
            }
        }

        if (a == strexp) {
            if (listener) {
                listener->panelChanged(Evlocallabstrexp, strexp->getTextValue());
            }
        }

        if (a == angexp) {
            if (listener) {
                listener->panelChanged(Evlocallabangexp, angexp->getTextValue());
            }
        }

        if (a == blendmaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskexp, blendmaskexp->getTextValue());
            }
        }

        if (a == radmaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskexp, radmaskexp->getTextValue());
            }
        }

        if (a == chromaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskexp, chromaskexp->getTextValue());
            }
        }

        if (a == gammaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskexp, gammaskexp->getTextValue());
            }
        }

        if (a == slomaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskexp, slomaskexp->getTextValue());
            }
        }

        if (a == lapmaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskexp, lapmaskexp->getTextValue());
            }
        }

        if (a == strmaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabstrmaskexp, strmaskexp->getTextValue());
            }
        }

        if (a == angmaskexp) {
            if (listener) {
                listener->panelChanged(Evlocallabangmaskexp, angmaskexp->getTextValue());
            }
        }

        if (a == softradiusexp) {
            if (listener) {
                listener->panelChanged(Evlocallabsoftradiusexp, softradiusexp->getTextValue());
            }
        }

        if (a == laplacexp) {
            if (listener) {
                listener->panelChanged(Evlocallablaplacexp, laplacexp->getTextValue());
            }
        }

        if (a == balanexp) {
            if (listener) {
                listener->panelChanged(Evlocallabbalanexp, balanexp->getTextValue());
            }
        }

        if (a == linear) {
            if (listener) {
                listener->panelChanged(Evlocallablinear, linear->getTextValue());
            }
        }

        if (a == gamm) {
            if (listener) {
                listener->panelChanged(Evlocallabgamm, gamm->getTextValue());
            }
        }

        if (a == fatamount) {
            if (listener) {
                listener->panelChanged(Evlocallabfatamount, fatamount->getTextValue());
            }
        }

        if (a == fatdetail) {
            if (listener) {
                listener->panelChanged(Evlocallabfatdetail, fatdetail->getTextValue());
            }
        }

        if (a == fatanchor) {
            if (listener) {
                listener->panelChanged(Evlocallabfatanchor, fatanchor->getTextValue());
            }
        }

        if (a == fatlevel) {
            if (listener) {
                listener->panelChanged(Evlocallabfatlevel, fatlevel->getTextValue());
            }
        }

    }

    if (getEnabled() && expshadhigh->getEnabled()) {

        if (a == multipliersh[0] || a == multipliersh[1] || a == multipliersh[2] || a == multipliersh[3] || a == multipliersh[4]) {
            if (listener) {
                listener->panelChanged(EvlocallabEqualizersh,
                                       Glib::ustring::compose("%1, %2, %3, %4, %5",
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multipliersh[0]->getIntValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multipliersh[1]->getIntValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multipliersh[2]->getIntValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multipliersh[3]->getIntValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multipliersh[4]->getIntValue())));
            }
        }


        if (a == highlights) {
            if (listener) {
                listener->panelChanged(Evlocallabhighlights, highlights->getTextValue());
            }
        }

        if (a == h_tonalwidth) {
            if (listener) {
                listener->panelChanged(Evlocallabh_tonalwidth, h_tonalwidth->getTextValue());
            }
        }

        if (a == shadows) {
            if (listener) {
                listener->panelChanged(Evlocallabshadows, shadows->getTextValue());
            }
        }

        if (a == s_tonalwidth) {
            if (listener) {
                listener->panelChanged(Evlocallabs_tonalwidth, s_tonalwidth->getTextValue());
            }
        }

        if (a == sh_radius) {
            if (listener) {
                listener->panelChanged(Evlocallabsh_radius, sh_radius->getTextValue());
            }
        }

        if (a == sensihs) {
            if (listener) {
                listener->panelChanged(Evlocallabsensihs, sensihs->getTextValue());
            }
        }

        if (a == blendmaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallabblendmaskSH, blendmaskSH->getTextValue());
            }
        }

        if (a == radmaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallabradmaskSH, radmaskSH->getTextValue());
            }
        }

        if (a == blurSHde) {
            if (listener) {
                listener->panelChanged(EvlocallabblurSHde, blurSHde->getTextValue());
            }
        }

        if (a == strSH) {
            if (listener) {
                listener->panelChanged(EvlocallabstrSH, strSH->getTextValue());
            }
        }

        if (a == angSH) {
            if (listener) {
                listener->panelChanged(EvlocallabangSH, angSH->getTextValue());
            }
        }

        if (a == chromaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallabchromaskSH, chromaskSH->getTextValue());
            }
        }

        if (a == gammaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallabgammaskSH, gammaskSH->getTextValue());
            }
        }

        if (a == slomaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallabslomaskSH, slomaskSH->getTextValue());
            }
        }

        if (a == lapmaskSH) {
            if (listener) {
                listener->panelChanged(EvlocallablapmaskSH, lapmaskSH->getTextValue());
            }
        }

        if (a == detailSH) {
            if (listener) {
                listener->panelChanged(EvlocallabdetailSH, detailSH->getTextValue());
            }
        }

        if (a == fatamountSH) {
            if (listener) {
                listener->panelChanged(EvlocallabfatamountSH, fatamountSH->getTextValue());
            }
        }


        if (a == fatanchorSH) {
            if (listener) {
                listener->panelChanged(EvlocallabfatanchorSH, fatanchorSH->getTextValue());
            }
        }

        if (a == gamSH) {
            if (listener) {
                listener->panelChanged(EvlocallabgamSH, gamSH->getTextValue());
            }
        }

        if (a == sloSH) {
            if (listener) {
                listener->panelChanged(EvlocallabsloSH, sloSH->getTextValue());
            }
        }

    }

    // Vibrance
    if (a == pastels && pastSatTog->get_active() && !(multiImage && pastSatTog->get_inconsistent())) {
        saturated->setValue(newval);
    }

    if (getEnabled() && expvibrance->getEnabled()) {
        if (a == saturated && !pastSatTog->get_active()) {
            if (listener) {
                listener->panelChanged(EvlocallabSaturated, saturated->getTextValue());
            }
        }

        if (a == pastels) {
            if (listener) {
                listener->panelChanged(EvlocallabPastels, pastels->getTextValue());
            }
        }

        if (a == sensiv) {
            if (listener) {
                listener->panelChanged(Evlocallabsensiv, sensiv->getTextValue());
            }
        }

        if (a == blendmaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskvi, blendmaskvib->getTextValue());
            }
        }

        if (a == radmaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskvib, radmaskvib->getTextValue());
            }
        }

        if (a == chromaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskvib, chromaskvib->getTextValue());
            }
        }

        if (a == gammaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskvib, gammaskvib->getTextValue());
            }
        }

        if (a == slomaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskvib, slomaskvib->getTextValue());
            }
        }

        if (a == lapmaskvib) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskvib, lapmaskvib->getTextValue());
            }
        }

        if (a == strvib) {
            if (listener) {
                listener->panelChanged(Evlocallabstrvib, strvib->getTextValue());
            }
        }

        if (a == strvibab) {
            if (listener) {
                listener->panelChanged(Evlocallabstrvibab, strvibab->getTextValue());
            }
        }

        if (a == strvibh) {
            if (listener) {
                listener->panelChanged(Evlocallabstrvibh, strvibh->getTextValue());
            }
        }

        if (a == angvib) {
            if (listener) {
                listener->panelChanged(Evlocallabangvib, angvib->getTextValue());
            }
        }

    }

    // Soft Light
    if (getEnabled() && expsoft->getEnabled()) {
        if (a == streng) {
            if (listener) {
                listener->panelChanged(Evlocallabstreng, streng->getTextValue());
            }
        }

        if (a == sensisf) {
            if (listener) {
                listener->panelChanged(Evlocallabsensisf, sensisf->getTextValue());
            }
        }

        if (a == laplace) {
            if (listener) {
                listener->panelChanged(Evlocallablaplace, laplace->getTextValue());
            }
        }

    }


    // Blur & Noise
    if (getEnabled() && (expblur->getEnabled())) {
        if (a == radius) {
            if (listener) {
                listener->panelChanged(Evlocallabradius, radius->getTextValue());
            }
        }

        if (a == strength) {
            if (listener) {
                listener->panelChanged(Evlocallabstrength, strength->getTextValue());
            }
        }

        if (a == sensibn) {
            if (listener) {
                listener->panelChanged(Evlocallabsensibn, sensibn->getTextValue());
            }
        }

        if (a == itera) {
            if (listener) {
                listener->panelChanged(Evlocallabitera, itera->getTextValue());
            }
        }

        if (a == isogr) {
            if (listener) {
                listener->panelChanged(Evlocallabisogr, isogr->getTextValue());
            }
        }

        if (a == strengr) {
            if (listener) {
                listener->panelChanged(Evlocallabstrengr, strengr->getTextValue());
            }
        }

        if (a == scalegr) {
            if (listener) {
                listener->panelChanged(Evlocallabscalegr, scalegr->getTextValue());
            }
        }

        if (a == guidbl) {
            if (listener) {
                listener->panelChanged(Evlocallabguidbl, guidbl->getTextValue());
            }
        }

        if (a == epsbl) {
            if (listener) {
                listener->panelChanged(Evlocallabepsbl, epsbl->getTextValue());
            }
        }
    }

    if (getEnabled() && (expblur->getEnabled() || expdenoi->getEnabled())) {
        if (a == blendmaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskbl, blendmaskbl->getTextValue());
            }
        }

        if (a == radmaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskbl, radmaskbl->getTextValue());
            }
        }

        if (a == chromaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskbl, chromaskbl->getTextValue());
            }
        }

        if (a == gammaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskbl, gammaskbl->getTextValue());
            }
        }

        if (a == slomaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskbl, slomaskbl->getTextValue());
            }
        }

        if (a == lapmaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskbl, lapmaskbl->getTextValue());
            }
        }

        if (a == shadmaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabshadmaskbl, shadmaskbl->getTextValue());
            }
        }

        if (a == strumaskbl) {
            if (listener) {
                listener->panelChanged(Evlocallabstrumaskbl, strumaskbl->getTextValue());
            }
        }

    }

    // Tone Mapping
    if (getEnabled() && exptonemap->getEnabled()) {
        if (a == stren) {
            if (listener) {
                listener->panelChanged(Evlocallabstren, stren->getTextValue());
            }
        }

        if (a == gamma) {
            if (listener) {
                listener->panelChanged(Evlocallabgamma, gamma->getTextValue());
            }
        }

        if (a == estop) {
            if (listener) {
                listener->panelChanged(Evlocallabestop, estop->getTextValue());
            }
        }

        if (a == scaltm) {
            if (listener) {
                listener->panelChanged(Evlocallabscaltm, scaltm->getTextValue());
            }
        }

        if (a == rewei) {
            if (listener) {
                listener->panelChanged(Evlocallabrewei, rewei->getTextValue());
            }
        }

        if (a == sensitm) {
            if (listener) {
                listener->panelChanged(Evlocallabsensitm, sensitm->getTextValue());
            }
        }

        if (a == softradiustm) {
            if (listener) {
                listener->panelChanged(Evlocallabsoftradiustm, softradiustm->getTextValue());
            }
        }

        if (a == amount) {
            if (listener) {
                listener->panelChanged(Evlocallabamount, amount->getTextValue());
            }
        }

        if (a == satur) {
            if (listener) {
                listener->panelChanged(Evlocallabsatur, satur->getTextValue());
            }
        }

        if (a == blendmasktm) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmasktm, blendmasktm->getTextValue());
            }
        }

        if (a == radmasktm) {
            if (listener) {
                listener->panelChanged(Evlocallabradmasktm, radmasktm->getTextValue());
            }
        }

        if (a == chromasktm) {
            if (listener) {
                listener->panelChanged(Evlocallabchromasktm, chromasktm->getTextValue());
            }
        }

        if (a == gammasktm) {
            if (listener) {
                listener->panelChanged(Evlocallabgammasktm, gammasktm->getTextValue());
            }
        }

        if (a == slomasktm) {
            if (listener) {
                listener->panelChanged(Evlocallabslomasktm, slomasktm->getTextValue());
            }
        }

        if (a == lapmasktm) {
            if (listener) {
                listener->panelChanged(Evlocallablapmasktm, lapmasktm->getTextValue());
            }
        }


    }

    // Retinex
    if (getEnabled() && expreti->getEnabled()) {
        if (a == str) {
            if (str->getValue() >= 0.1f) {
                retitoolFrame->show();
            } else {
                retitoolFrame->hide();
            }

            if (listener) {
                listener->panelChanged(Evlocallabstr, str->getTextValue());
            }
        }

        if (a == chrrt) {
            if (listener) {
                listener->panelChanged(Evlocallabchrrt, chrrt->getTextValue());
            }
        }

        if (a == neigh) {
            if (listener) {
                listener->panelChanged(Evlocallabneigh, neigh->getTextValue());
            }
        }

        if (a == vart) {
            if (listener) {
                listener->panelChanged(Evlocallabvart, vart->getTextValue());
            }
        }

        if (a == offs) {
            if (listener) {
                listener->panelChanged(Evlocallaboffs, offs->getTextValue());
            }
        }

        if (a == dehaz) {
            if (listener) {
                listener->panelChanged(Evlocallabdehaz, dehaz->getTextValue());
            }
        }

        if (a == depth) {
            if (listener) {
                listener->panelChanged(Evlocallabdepth, depth->getTextValue());
            }
        }

        if (a == sensih) {
            if (listener) {
                listener->panelChanged(Evlocallabsensih, sensih->getTextValue());
            }
        }

        if (a == softradiusret) {
            if (listener) {
                listener->panelChanged(Evlocallabsoftradiusret, softradiusret->getTextValue());
            }
        }

        if (a == blendmaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskreti, blendmaskreti->getTextValue());
            }
        }

        if (a == radmaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskreti, radmaskreti->getTextValue());
            }
        }

        if (a == chromaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskreti, chromaskreti->getTextValue());
            }
        }

        if (a == gammaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskreti, gammaskreti->getTextValue());
            }
        }

        if (a == slomaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskreti, slomaskreti->getTextValue());
            }
        }

        if (a == lapmaskreti) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskreti, lapmaskreti->getTextValue());
            }
        }

        if (a == scalereti) {
            if (scalereti->getValue() == 1) {
                LocalcurveEditorgainT->hide();
                LocalcurveEditortransT->hide();
                softradiusret->hide();
                retinexMethod->hide();
            } else {
                LocalcurveEditorgainT->show();
                LocalcurveEditortransT->show();
                softradiusret->show();
                retinexMethod->show();
            }

            if (listener) {
                listener->panelChanged(Evlocallabscalereti, scalereti->getTextValue());
            }
        }

        if (a == darkness) {
            if (listener) {
                listener->panelChanged(Evlocallabdarkness, darkness->getTextValue());
            }
        }

        if (a == lightnessreti) {
            if (listener) {
                listener->panelChanged(Evlocallablightnessreti, lightnessreti->getTextValue());
            }
        }

        if (a == limd) {
            if (listener) {
                listener->panelChanged(Evlocallablimd, limd->getTextValue());
            }
        }

        if (a == cliptm) {
            if (listener) {
                listener->panelChanged(Evlocallabcliptm, cliptm->getTextValue());
            }
        }

    }

    // Sharpening
    if (getEnabled() && expsharp->getEnabled()) {
        if (a == sharcontrast) {
            if (listener) {
                listener->panelChanged(Evlocallabsharcontrast, sharcontrast->getTextValue());
            }
        }

        if (a == sharradius) {
            if (listener) {
                listener->panelChanged(Evlocallabsharradius, sharradius->getTextValue());
            }
        }

        if (a == sharamount) {
            if (listener) {
                listener->panelChanged(Evlocallabsharamount, sharamount->getTextValue());
            }
        }

        if (a == shardamping) {
            if (listener) {
                listener->panelChanged(Evlocallabshardamping, shardamping->getTextValue());
            }
        }

        if (a == shariter) {
            if (listener) {
                listener->panelChanged(Evlocallabshariter, shariter->getTextValue());
            }
        }

        if (a == sharblur) {
            if (listener) {
                listener->panelChanged(Evlocallabsharblur, sharblur->getTextValue());
            }
        }

        if (a == sensisha) {
            if (listener) {
                listener->panelChanged(Evlocallabsensis, sensisha->getTextValue());
            }
        }
    }

    // Local Contrast
    if (getEnabled() && expcontrast->getEnabled()) {
        if (a == lcradius) {
            if (listener) {
                listener->panelChanged(Evlocallablcradius, lcradius->getTextValue());
            }
        }

        if (a == lcamount) {
            if (listener) {
                listener->panelChanged(Evlocallablcamount, lcamount->getTextValue());
            }
        }

        if (a == lcdarkness) {
            if (listener) {
                listener->panelChanged(Evlocallablcdarkness, lcdarkness->getTextValue());
            }
        }

        if (a == lclightness) {
            if (listener) {
                listener->panelChanged(Evlocallablclightness, lclightness->getTextValue());
            }
        }

        if (a == levelwav) {
            if (listener) {
                listener->panelChanged(Evlocallablevelwav, levelwav->getTextValue());
            }
        }

        if (a == residcont) {
            if (listener) {
                listener->panelChanged(Evlocallabresidcont, residcont->getTextValue());
            }
        }

        if (a == residchro) {
            if (listener) {
                listener->panelChanged(Evlocallabresidchro, residchro->getTextValue());
            }
        }

        if (a == clarilres) {
            if (listener) {
                listener->panelChanged(Evlocallabclarilres, clarilres->getTextValue());
            }
        }

        if (a == claricres) {
            if (listener) {
                listener->panelChanged(Evlocallabclaricres, claricres->getTextValue());
            }
        }

        if (a == clarisoft) {
            if (listener) {
                listener->panelChanged(Evlocallabclarisoft, clarisoft->getTextValue());
            }
        }

        if (a == sensilc) {
            if (listener) {
                listener->panelChanged(Evlocallabsensilc, sensilc->getTextValue());
            }
        }
    }

    // Contrast by detail levels
    if (getEnabled() && expcbdl->getEnabled()) {
        if (a == multiplier[0] || a == multiplier[1] || a == multiplier[2] || a == multiplier[3] || a == multiplier[4] || a == multiplier[5]) {
            if (listener) {
                listener->panelChanged(EvlocallabEqualizer,
                                       Glib::ustring::compose("%1, %2, %3, %4, %5, %6",
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[0]->getValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[1]->getValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[2]->getValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[3]->getValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[4]->getValue()),
                                               Glib::ustring::format(std::fixed, std::setprecision(2), multiplier[5]->getValue())));
            }
        }

        if (a == chromacbdl) {
            if (listener) {
                listener->panelChanged(Evlocallabchromacbdl, chromacbdl->getTextValue());
            }
        }

        if (a == threshold) {
            if (listener) {
                listener->panelChanged(EvlocallabThresho, threshold->getTextValue());
            }
        }

        if (a == sensicb) {
            if (listener) {
                listener->panelChanged(Evlocallabsensicb, sensicb->getTextValue());
            }
        }

        if (a == clarityml) {
            //contresid->setValue(0.);

            if (listener) {
                listener->panelChanged(EvLocallabclarityml, clarityml->getTextValue());
            }
        }

        if (a == contresid) {

            if (listener) {
                listener->panelChanged(EvLocallabcontresid, contresid->getTextValue());
            }
        }

        if (a == blurcbdl) {

            if (listener) {
                listener->panelChanged(EvLocallabblurcbdl, blurcbdl->getTextValue());
            }
        }

        if (a == softradiuscb) {
            if (listener) {
                listener->panelChanged(Evlocallabsoftradiuscb, softradiuscb->getTextValue());
            }
        }

        if (a == blendmaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallabblendmaskcb, blendmaskcb->getTextValue());
            }
        }

        if (a == radmaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallabradmaskcb, radmaskcb->getTextValue());
            }
        }

        if (a == chromaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallabchromaskcb, chromaskcb->getTextValue());
            }
        }

        if (a == gammaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallabgammaskcb, gammaskcb->getTextValue());
            }
        }

        if (a == slomaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallabslomaskcb, slomaskcb->getTextValue());
            }
        }

        if (a == lapmaskcb) {
            if (listener) {
                listener->panelChanged(Evlocallablapmaskcb, lapmaskcb->getTextValue());
            }
        }

    }

    // Denoise
    if (getEnabled() && expdenoi->getEnabled()) {
        if (a == noiselumf) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselumf, noiselumf->getTextValue());
            }
        }

        if (a == noiselumf0) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselumf0, noiselumf0->getTextValue());
            }
        }

        if (a == noiselumf2) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselumf2, noiselumf2->getTextValue());
            }
        }

        if (a == noiselumc) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselumc, noiselumc->getTextValue());
            }
        }

        if (a == noiselumdetail) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselumdetail, noiselumdetail->getTextValue());
            }
        }

        if (a == noiselequal) {
            if (listener) {
                listener->panelChanged(Evlocallabnoiselequal, noiselequal->getTextValue());
            }
        }

        if (a == noisechrof) {
            if (listener) {
                listener->panelChanged(Evlocallabnoisechrof, noisechrof->getTextValue());
            }
        }

        if (a == noisechroc) {
            if (listener) {
                listener->panelChanged(Evlocallabnoisechroc, noisechroc->getTextValue());
            }
        }

        if (a == noisechrodetail) {
            if (listener) {
                listener->panelChanged(Evlocallabnoisechrodetail, noisechrodetail->getTextValue());
            }
        }

        if (a == adjblur) {
            if (listener) {
                listener->panelChanged(Evlocallabadjblur, adjblur->getTextValue());
            }
        }

        if (a == bilateral) {
            if (listener) {
                listener->panelChanged(Evlocallabbilateral, bilateral->getTextValue());
            }
        }

        if (a == sensiden) {
            if (listener) {
                listener->panelChanged(Evlocallabsensiden, sensiden->getTextValue());
            }
        }

        if (a == detailthr) {
            if (listener) {
                listener->panelChanged(Evlocallabdetailthr, detailthr->getTextValue());
            }
        }

    }
}

void Locallab::enabledChanged()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvlocallabEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvlocallabEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvlocallabEnabled, M("GENERAL_DISABLED"));
        }
    }
}

void Locallab::trimValues(rtengine::procparams::ProcParams * pp)
{
    // TODO
}

void Locallab::setBatchMode(bool batchMode)
{
    // printf("BatchMode : %d\n", batchMode);

    ToolPanel::setBatchMode(batchMode);

    // Set batch mode for controlspotpanel
    expsettings->setBatchMode(batchMode);

    // Set batch mode for adjusters and threshold adjusters
    // Color & Light
    lightness->showEditedCB();
    contrast->showEditedCB();
    chroma->showEditedCB();
    sensi->showEditedCB();
    structcol->showEditedCB();
    strcol->showEditedCB();
    strcolab->showEditedCB();
    strcolh->showEditedCB();
    angcol->showEditedCB();
    strengthgrid->showEditedCB();
    blurcolde->showEditedCB();
    blurcol->showEditedCB();
    contcol->showEditedCB();
    blendmaskcol->showEditedCB();
    radmaskcol->showEditedCB();
    chromaskcol->showEditedCB();
    gammaskcol->showEditedCB();
    slomaskcol->showEditedCB();
    shadmaskcol->showEditedCB();
    strumaskcol->showEditedCB();
    lapmaskcol->showEditedCB();
    softradiuscol->showEditedCB();
    opacol->showEditedCB();
    mercol->showEditedCB();
    merlucol->showEditedCB();
    conthrcol->showEditedCB();
    csThresholdcol->showEditedCB();
    // Exposure
    expcomp->showEditedCB();
    hlcompr->showEditedCB();
    hlcomprthresh->showEditedCB();
    black->showEditedCB();
    shadex->showEditedCB();
    shcompr->showEditedCB();
    expchroma->showEditedCB();
    warm->showEditedCB();
    sensiex->showEditedCB();
    structexp->showEditedCB();
    blurexpde->showEditedCB();
    strexp->showEditedCB();
    angexp->showEditedCB();
    blendmaskexp->showEditedCB();
    radmaskexp->showEditedCB();
    chromaskexp->showEditedCB();
    gammaskexp->showEditedCB();
    slomaskexp->showEditedCB();
    lapmaskexp->showEditedCB();
    strmaskexp->showEditedCB();
    angmaskexp->showEditedCB();
    softradiusexp->showEditedCB();
    laplacexp->showEditedCB();
    balanexp->showEditedCB();
    linear->showEditedCB();
    gamm->showEditedCB();
    fatamount->showEditedCB();
    fatdetail->showEditedCB();
    fatanchor->showEditedCB();
    fatlevel->showEditedCB();
    //Shadow Highlight
    highlights->showEditedCB();
    h_tonalwidth->showEditedCB();
    shadows->showEditedCB();
    s_tonalwidth->showEditedCB();
    sh_radius->showEditedCB();
    sensihs->showEditedCB();
    blendmaskSH->showEditedCB();
    radmaskSH->showEditedCB();
    blurSHde->showEditedCB();
    strSH->showEditedCB();
    angSH->showEditedCB();
    chromaskSH->showEditedCB();
    gammaskSH->showEditedCB();
    slomaskSH->showEditedCB();
    lapmaskSH->showEditedCB();
    detailSH->showEditedCB();

    for (int i = 0; i < 5; i++) {
        multipliersh[i]->showEditedCB();
    }

    fatamountSH->showEditedCB();
    fatanchorSH->showEditedCB();
    gamSH->showEditedCB();
    sloSH->showEditedCB();

    // Vibrance
    saturated->showEditedCB();
    pastels->showEditedCB();
    psThreshold->showEditedCB();
    sensiv->showEditedCB();
    blendmaskvib->showEditedCB();
    radmaskvib->showEditedCB();
    chromaskvib->showEditedCB();
    gammaskvib->showEditedCB();
    slomaskvib->showEditedCB();
    lapmaskvib->showEditedCB();
    strvib->showEditedCB();
    strvibab->showEditedCB();
    strvibh->showEditedCB();
    angvib->showEditedCB();
    // Soft Light
    streng->showEditedCB();
    sensisf->showEditedCB();
    laplace->showEditedCB();
    // Blur & Noise
    radius->showEditedCB();
    streng->showEditedCB();
    strength->showEditedCB();
    sensibn->showEditedCB();
    itera->showEditedCB();
    isogr->showEditedCB();
    strengr->showEditedCB();
    scalegr->showEditedCB();
    guidbl->showEditedCB();
    epsbl->showEditedCB();
    blendmaskbl->showEditedCB();
    radmaskbl->showEditedCB();
    chromaskbl->showEditedCB();
    gammaskbl->showEditedCB();
    slomaskbl->showEditedCB();
    lapmaskbl->showEditedCB();
    shadmaskbl->showEditedCB();
    strumaskbl->showEditedCB();
    csThresholdblur->showEditedCB();
    // Tone Mapping
    stren->showEditedCB();
    gamma->showEditedCB();
    estop->showEditedCB();
    scaltm->showEditedCB();
    rewei->showEditedCB();
    satur->showEditedCB();
    sensitm->showEditedCB();
    softradiustm->showEditedCB();
    amount->showEditedCB();
    blendmasktm->showEditedCB();
    radmasktm->showEditedCB();
    chromasktm->showEditedCB();
    gammasktm->showEditedCB();
    slomasktm->showEditedCB();
    lapmasktm->showEditedCB();
    // Retinex
    str->showEditedCB();
    chrrt->showEditedCB();
    neigh->showEditedCB();
    vart->showEditedCB();
    offs->showEditedCB();
    dehaz->showEditedCB();
    depth->showEditedCB();
    sensih->showEditedCB();
    softradiusret->showEditedCB();
    blendmaskreti->showEditedCB();
    radmaskreti->showEditedCB();
    chromaskreti->showEditedCB();
    gammaskreti->showEditedCB();
    slomaskreti->showEditedCB();
    lapmaskreti->showEditedCB();
    scalereti->showEditedCB();
    darkness->showEditedCB();
    lightnessreti->showEditedCB();
    limd->showEditedCB();
    cliptm->showEditedCB();
    // Sharpening
    sharradius->showEditedCB();
    sharamount->showEditedCB();
    shardamping->showEditedCB();
    shariter->showEditedCB();
    sensisha->showEditedCB();
    // Local Contrast
    lcradius->showEditedCB();
    lcamount->showEditedCB();
    lcdarkness->showEditedCB();
    lclightness->showEditedCB();
    levelwav->showEditedCB();
    residcont->showEditedCB();
    residchro->showEditedCB();
    clarilres->showEditedCB();
    claricres->showEditedCB();
    clarisoft->showEditedCB();
    csThreshold->showEditedCB();
    sensilc->showEditedCB();

    // Contrast by detail levels
    for (int i = 0; i < 6; i++) {
        multiplier[i]->showEditedCB();
    }

    chromacbdl->showEditedCB();
    threshold->showEditedCB();
    sensicb->showEditedCB();
    clarityml->showEditedCB();
    contresid->showEditedCB();
    blurcbdl->showEditedCB();
    softradiuscb->showEditedCB();
    blendmaskcb->showEditedCB();
    radmaskcb->showEditedCB();
    chromaskcb->showEditedCB();
    gammaskcb->showEditedCB();
    slomaskcb->showEditedCB();
    lapmaskcb->showEditedCB();

    // Denoise
    noiselumf->showEditedCB();
    noiselumc->showEditedCB();
    noiselumdetail->showEditedCB();
    noiselequal->showEditedCB();
    noiselumf0->showEditedCB();
    noiselumf2->showEditedCB();
    noisechroc->showEditedCB();
    noisechrodetail->showEditedCB();
    adjblur->showEditedCB();
    bilateral->showEditedCB();
    sensiden->showEditedCB();
    detailthr->showEditedCB();

    // Set batch mode for comboBoxText
    // Color & Light
    qualitycurveMethod->append(M("GENERAL_UNCHANGED"));
    gridMethod->append(M("GENERAL_UNCHANGED"));
    merMethod->append(M("GENERAL_UNCHANGED"));
    toneMethod->append(M("GENERAL_UNCHANGED"));
    mergecolMethod->append(M("GENERAL_UNCHANGED"));
    //exposure
    expMethod->append(M("GENERAL_UNCHANGED"));
    exnoiseMethod->append(M("GENERAL_UNCHANGED"));
    // softlight
    shMethod->append(M("GENERAL_UNCHANGED"));
    softMethod->append(M("GENERAL_UNCHANGED"));
    // Blur & Noise
    blMethod->append(M("GENERAL_UNCHANGED"));
    blurMethod->append(M("GENERAL_UNCHANGED"));
    medMethod->append(M("GENERAL_UNCHANGED"));
    // Retinex
    retinexMethod->append(M("GENERAL_UNCHANGED"));

    //Local contrast
    localcontMethod->append(M("GENERAL_UNCHANGED"));

    // In batch mode, being able to change mask visibility is useless
    showmaskcolMethod->hide();
    showmaskcolMethodinv->hide();
    showmaskexpMethod->hide();
    showmaskexpMethodinv->hide();
    showmaskSHMethod->hide();
    showmaskSHMethodinv->hide();
    showmaskvibMethod->hide();
    showmaskcbMethod->hide();
    showmaskretiMethod->hide();
    showmasktmMethod->hide();
    showmaskblMethod->hide();
}

std::vector<double> Locallab::getCurvePoints(ThresholdSelector* tAdjuster) const
{
    std::vector<double> points;
    double threshold, transitionWeighting;
    tAdjuster->getPositions<double> (transitionWeighting, threshold); // ( range -100;+100,   range 0;+100 )
    transitionWeighting /= 100.; // range -1., +1.
    threshold /= 100.;      // range  0., +1.

    // Initial point
    points.push_back(0.);
    points.push_back(0.);

    double p2 = 3.0 * threshold / 4.0;             // same one than in ipvibrance.cc
    double s0 = threshold + (1.0 - threshold) / 4.0; // same one than in ipvibrance.cc

    // point at the beginning of the first linear transition
    points.push_back(p2);
    points.push_back(0.);

    // Y value of the chroma mean point, calculated to get a straight line between p2 and s0
    double chromaMean = (threshold / 4.0) / (s0 - p2);

    // move chromaMean up or down depending on transitionWeighting
    if (transitionWeighting > 0.0) {
        // positive values -> give more weight to Saturated
        chromaMean = (1.0 - chromaMean) * transitionWeighting + chromaMean;
    } else if (transitionWeighting < 0.0) {
        // negative values -> give more weight to Pastels
        chromaMean = chromaMean  * transitionWeighting + chromaMean;
    }

    // point at the location of the Top cursor, at the end of the first linear transition and the beginning of the second one
    points.push_back(threshold);
    points.push_back(chromaMean);

    if (threshold < 1.0) {

        // point at the end of the second linear transition
        points.push_back(s0);
        points.push_back(1.0);

        // end point
        points.push_back(1.0);
        points.push_back(1.0);
    }

    return points;
}

void Locallab::setEditProvider(EditDataProvider * provider)
{
    expsettings->setEditProvider(provider);
}

void Locallab::subscribe()
{
    expsettings->subscribe();
}

void Locallab::unsubscribe()
{
    expsettings->unsubscribe();
}

void Locallab::colorForValue(double valX, double valY, enum ColorCaller::ElemType elemType, int callerId, ColorCaller *caller)
{

    float R = 0.f;
    float G = 0.f;
    float B = 0.f;

    if (elemType == ColorCaller::CCET_VERTICAL_BAR) {
        valY = 0.5;
    }

    if (callerId == 1) {         // ch - main curve

        Color::hsv2rgb01(float (valX), float (valY), 0.5f, R, G, B);
    } else if (callerId == 2) {  // cc - bottom bar

        float value = (1.f - 0.7f) * float (valX) + 0.7f;
        // whole hue range
        // Y axis / from 0.15 up to 0.75 (arbitrary values; was 0.45 before)
        Color::hsv2rgb01(float (valY), float (valX), value, R, G, B);
    } else if (callerId == 3) {  // lc - bottom bar

        float value = (1.f - 0.7f) * float (valX) + 0.7f;
        // Y axis / from 0.15 up to 0.75 (arbitrary values; was 0.45 before)
        Color::hsv2rgb01(float (valY), float (valX), value, R, G, B);
    } else if (callerId == 4) {  // LH - bottom bar
        Color::hsv2rgb01(float (valX), 0.5f, float (valY), R, G, B);
    } else if (callerId == 5) {  // HH - bottom bar
        float h = float ((valY - 0.5) * 0.3 + valX);

        if (h > 1.0f) {
            h -= 1.0f;
        } else if (h < 0.0f) {
            h += 1.0f;
        }

        Color::hsv2rgb01(h, 0.5f, 0.5f, R, G, B);
    } else if (callerId == 6) {
        // TODO
        float x = valX - 1.f / 6.f;

        if (x < 0.f) {
            x += 1.f;
        }

        x = log2lin(x, 3.f);
        // float x = valX;
        Color::hsv2rgb01(x, 0.5f, 0.5f, R, G, B);
    } else if (callerId == 7) {
        Color::hsv2rgb01(float(valY), float(valX), 0.5f, R, G, B);
    }

    caller->ccRed = double (R);
    caller->ccGreen = double (G);
    caller->ccBlue = double (B);
}

void Locallab::setListener(ToolPanelListener* tpl)
{
    this->listener = tpl;
    labgrid->setListener(tpl);
    labgridmerg->setListener(tpl);
    expsettings->setListener(tpl);
}

void Locallab::enableListener()
{
    // printf("enableListener\n");

    FoldableToolPanel::enableListener();
    // Color & Light
    enablecolorConn.block(false);
    curvactivConn.block(false);
    qualitycurveMethodConn.block(false);
    gridMethodConn.block(false);
    merMethodConn.block(false);
    toneMethodConn.block(false);
    mergecolMethodConn.block(false);
    inversConn.block(false);
    specialConn.block(false);
    toolcolConn.block(false);
    showmaskcolMethodConn.block(false);
    showmaskcolMethodConninv.block(false);
    enaColorMaskConn.block(false);
    fftColorMaskConn.block(false);
    // Exposure
    enableexposeConn.block(false);
    inversexConn.block(false);
    showmaskexpMethodConn.block(false);
    showmaskexpMethodConninv.block(false);
    expMethodConn.block(false);
    exnoiseMethodConn.block(false);
    enaExpMaskConn.block(false);
    enaExpMaskaftConn.block(false);
    // Shadow highlight
    shMethodConn.block(false);
    enableshadhighConn.block(false);
    showmaskSHMethodConn.block(false);
    showmaskSHMethodConninv.block(false);
    enaSHMaskConn.block(false);
    inversshConn.block(false);
    // Vibrance
    enablevibranceConn.block(false);
    pskinsconn.block(false);
    ashiftconn.block(false);
    pastsattogconn.block(false);
    showmaskvibMethodConn.block(false);
    enavibMaskConn.block(false);
    // Soft Light
    enablesoftConn.block(false);
    softMethodConn.block(false);
    showmasksoftMethodConn.block(false);
    // Blur & Noise
    enableblurConn.block(false);
    blMethodConn.block(false);
    blurMethodConn.block(false);
    medMethodConn.block(false);
    activlumConn.block(false);
    enablMaskConn.block(false);
    fftwblConn.block(false);
    toolblConn.block(false);
    showmaskblMethodConn.block(false);
    // Tone Mapping
    enabletonemapConn.block(false);
    equiltmConn.block(false);
    enatmMaskConn.block(false);
    enatmMaskaftConn.block(false);
    showmasktmMethodConn.block(false);
    // Retinex
    enableretiConn.block(false);
    retinexMethodConn.block(false);
    inversretConn.block(false);
    equilretConn.block(false);
    loglinConn.block(false);
    lumonlyConn.block(false);
    enaretiMaskConn.block(false);
    enaretiMasktmapConn.block(false);
    showmaskretiMethodConn.block(false);
    fftwretiConn.block(false);
    // Sharpening
    enablesharpConn.block(false);
    inversshaConn.block(false);
    // Local Contrast
    enablecontrastConn.block(false);
    localcontMethodConn.block(false);
    fftwlcConn.block(false);
    // Contrast by detail levels
    enablecbdlConn.block(false);
    enacbMaskConn.block(false);
    showmaskcbMethodConn.block(false);
    // Denoise
    enabledenoiConn.block(false);
}

void Locallab::disableListener()
{
    // printf("disableListener\n");

    FoldableToolPanel::disableListener();
    // Color & Light
    enablecolorConn.block(true);
    curvactivConn.block(true);
    qualitycurveMethodConn.block(true);
    gridMethodConn.block(true);
    merMethodConn.block(true);
    toneMethodConn.block(true);
    mergecolMethodConn.block(true);
    inversConn.block(true);
    specialConn.block(true);
    toolcolConn.block(true);
    showmaskcolMethodConn.block(true);
    showmaskcolMethodConninv.block(true);
    fftColorMaskConn.block(true);
    // Exposure
    enableexposeConn.block(true);
    inversexConn.block(true);
    showmaskexpMethodConn.block(true);
    showmaskexpMethodConninv.block(true);
    expMethodConn.block(true);
    exnoiseMethodConn.block(true);
    enaExpMaskConn.block(true);
    enaExpMaskaftConn.block(true);
    // Shadow highlight
    shMethodConn.block(true);
    enableshadhighConn.block(true);
    showmaskSHMethodConn.block(true);
    showmaskSHMethodConninv.block(true);
    enaSHMaskConn.block(true);
    inversshConn.block(true);
    // Vibrance
    enablevibranceConn.block(true);
    pskinsconn.block(true);
    ashiftconn.block(true);
    pastsattogconn.block(true);
    showmaskvibMethodConn.block(true);
    enavibMaskConn.block(true);
    // Soft Light
    enablesoftConn.block(true);
    softMethodConn.block(true);
    showmasksoftMethodConn.block(true);
    // Blur & Noise
    enableblurConn.block(true);
    blMethodConn.block(true);
    blurMethodConn.block(true);
    medMethodConn.block(true);
    activlumConn.block(true);
    enablMaskConn.block(true);
    fftwblConn.block(true);
    toolblConn.block(true);
    showmaskblMethodConn.block(true);
    // Tone Mapping
    enabletonemapConn.block(true);
    equiltmConn.block(true);
    enatmMaskConn.block(true);
    enatmMaskaftConn.block(true);
    showmasktmMethodConn.block(true);
    // Retinex
    enableretiConn.block(true);
    retinexMethodConn.block(true);
    inversretConn.block(true);
    equilretConn.block(true);
    loglinConn.block(true);
    lumonlyConn.block(true);
    enaretiMaskConn.block(true);
    enaretiMasktmapConn.block(true);
    showmaskretiMethodConn.block(true);
    fftwretiConn.block(true);
    // Sharpening
    enablesharpConn.block(true);
    inversshaConn.block(true);
    // Local Contrast
    enablecontrastConn.block(true);
    localcontMethodConn.block(true);
    fftwlcConn.block(true);
    // Contrast by detail levels
    enablecbdlConn.block(true);
    enacbMaskConn.block(true);
    showmaskcbMethodConn.block(true);
    // Denoise
    enabledenoiConn.block(true);
}

void Locallab::updateLocallabGUI(const rtengine::procparams::ProcParams* pp, const ParamsEdited* pedited, int index)
{
    // printf("updateLocallabGUI\n");

    // Update GUI values
    if (index < pp->locallab.nbspot && index < (int)pp->locallab.spots.size()) {
        // Color & Light
        expcolor->setEnabled(pp->locallab.spots.at(index).expcolor);
        curvactiv->set_active(pp->locallab.spots.at(index).curvactiv);
        lightness->setValue(pp->locallab.spots.at(index).lightness);
        contrast->setValue(pp->locallab.spots.at(index).contrast);
        chroma->setValue(pp->locallab.spots.at(index).chroma);
        labgrid->setParams(pp->locallab.spots.at(index).labgridALow / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridBLow / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridAHigh / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridBHigh / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, false);
        //labgridmerg->setParams(pp->locallab.spots.at(index).labgridALowmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridBLowmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridAHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridBHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, false);
        labgridmerg->setParams(0, 0, pp->locallab.spots.at(index).labgridAHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, pp->locallab.spots.at(index).labgridBHighmerg / rtengine::procparams::LocallabParams::LABGRIDL_CORR_MAX, false);

        if (complexsoft == 2) {
            labgrid->setParams(0, 0, 0, 0, false);
        }

        strengthgrid->setValue(pp->locallab.spots.at(index).strengthgrid);
        sensi->setValue(pp->locallab.spots.at(index).sensi);
        structcol->setValue(pp->locallab.spots.at(index).structcol);
        strcol->setValue(pp->locallab.spots.at(index).strcol);
        angcol->setValue(pp->locallab.spots.at(index).angcol);
        strcolab->setValue(pp->locallab.spots.at(index).strcolab);
        strcolh->setValue(pp->locallab.spots.at(index).strcolh);

        if (pp->locallab.spots.at(index).qualitycurveMethod == "none") {
            qualitycurveMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).qualitycurveMethod == "std") {
            qualitycurveMethod->set_active(1);
        }

        if (pp->locallab.spots.at(index).gridMethod == "one") {
            gridMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).gridMethod == "two") {
            gridMethod->set_active(1);
        }

        if (pp->locallab.spots.at(index).merMethod == "mone") {
            merMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).merMethod == "mtwo") {
            merMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).merMethod == "mthr") {
            merMethod->set_active(2);
        } else if (pp->locallab.spots.at(index).merMethod == "mfou") {
            merMethod->set_active(3);
        } else if (pp->locallab.spots.at(index).merMethod == "mfiv") {
            merMethod->set_active(4);
        }

        if (pp->locallab.spots.at(index).toneMethod == "one") {
            toneMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).toneMethod == "two") {
            toneMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).toneMethod == "thr") {
            toneMethod->set_active(2);
        } else if (pp->locallab.spots.at(index).toneMethod == "fou") {
            toneMethod->set_active(3);
        }

        if (pp->locallab.spots.at(index).mergecolMethod == "one") {
            mergecolMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "two") {
            mergecolMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "thr") {
            mergecolMethod->set_active(2);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "fou") {
            mergecolMethod->set_active(3);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "fiv") {
            mergecolMethod->set_active(4);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "six") {
            mergecolMethod->set_active(5);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "sev") {
            mergecolMethod->set_active(6);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "sev0") {
            mergecolMethod->set_active(7);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "sev1") {
            mergecolMethod->set_active(8);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "sev2") {
            mergecolMethod->set_active(9);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "hei") {
            mergecolMethod->set_active(10);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "nin") {
            mergecolMethod->set_active(11);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "ten") {
            mergecolMethod->set_active(12);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "ele") {
            mergecolMethod->set_active(13);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "twe") {
            mergecolMethod->set_active(14);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "thi") {
            mergecolMethod->set_active(15);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "for") {
            mergecolMethod->set_active(16);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "hue") {
            mergecolMethod->set_active(17);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "sat") {
            mergecolMethod->set_active(18);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "col") {
            mergecolMethod->set_active(19);
        } else if (pp->locallab.spots.at(index).mergecolMethod == "lum") {
            mergecolMethod->set_active(20);
        }


        if (pp->locallab.spots.at(index).scalereti == 1) {
            LocalcurveEditorgainT->hide();
            LocalcurveEditortransT->hide();
            softradiusret->hide();
            retinexMethod->hide();
        } else {
            LocalcurveEditorgainT->show();
            LocalcurveEditortransT->show();
            softradiusret->show();
            retinexMethod->show();
        }


        if (complexsoft < 2) {
            llshape->setCurve(pp->locallab.spots.at(index).llcurve);
            lcshape->setCurve(pp->locallab.spots.at(index).lccurve);
        }

        ccshape->setCurve(pp->locallab.spots.at(index).cccurve);
        clshape->setCurve(pp->locallab.spots.at(index).clcurve);
        rgbshape->setCurve(pp->locallab.spots.at(index).rgbcurve);

        if (complexsoft == 2) {
            lcshape->reset();
            clshape->reset();
            LHshape->reset();
            HHshape->reset();
            rgbshape->reset();
        }

        LHshape->setCurve(pp->locallab.spots.at(index).LHcurve);
        HHshape->setCurve(pp->locallab.spots.at(index).HHcurve);
        invers->set_active(pp->locallab.spots.at(index).invers);
        special->set_active(pp->locallab.spots.at(index).special);
        toolcol->set_active(pp->locallab.spots.at(index).toolcol);
        enaColorMask->set_active(pp->locallab.spots.at(index).enaColorMask);
        fftColorMask->set_active(pp->locallab.spots.at(index).fftColorMask);
        CCmaskshape->setCurve(pp->locallab.spots.at(index).CCmaskcurve);
        LLmaskshape->setCurve(pp->locallab.spots.at(index).LLmaskcurve);
        HHmaskshape->setCurve(pp->locallab.spots.at(index).HHmaskcurve);
        HHhmaskshape->setCurve(pp->locallab.spots.at(index).HHhmaskcurve);
        blurcolde->setValue(pp->locallab.spots.at(index).blurcolde);
        blurcol->setValue(pp->locallab.spots.at(index).blurcol);
        contcol->setValue(pp->locallab.spots.at(index).contcol);
        blendmaskcol->setValue(pp->locallab.spots.at(index).blendmaskcol);
        radmaskcol->setValue(pp->locallab.spots.at(index).radmaskcol);
        chromaskcol->setValue(pp->locallab.spots.at(index).chromaskcol);
        gammaskcol->setValue(pp->locallab.spots.at(index).gammaskcol);
        slomaskcol->setValue(pp->locallab.spots.at(index).slomaskcol);

        shadmaskcol->setValue(pp->locallab.spots.at(index).shadmaskcol);
        strumaskcol->setValue(pp->locallab.spots.at(index).strumaskcol);
        lapmaskcol->setValue(pp->locallab.spots.at(index).lapmaskcol);
        softradiuscol->setValue(pp->locallab.spots.at(index).softradiuscol);
        opacol->setValue(pp->locallab.spots.at(index).opacol);
        mercol->setValue(pp->locallab.spots.at(index).mercol);
        merlucol->setValue(pp->locallab.spots.at(index).merlucol);
        conthrcol->setValue(pp->locallab.spots.at(index).conthrcol);
        Lmaskshape->setCurve(pp->locallab.spots.at(index).Lmaskcurve);
        LLmaskcolshapewav->setCurve(pp->locallab.spots.at(index).LLmaskcolcurvewav);
        csThresholdcol->setValue<int>(pp->locallab.spots.at(index).csthresholdcol);

        if (complexsoft == 2) {
            HHhmaskshape->reset();
        }

        if (complexsoft == 2) {
            gammaskcol->setValue(1);
            slomaskcol->setValue(0);
            shadmaskcol->setValue(0);
            strumaskcol->setValue(0);
            strcolab->setValue(0);
            strcolh->setValue(0);
            blurcolde->setValue(5);
            softradiuscol->setValue(0);
            special->set_active(false);
            toolcol->set_active(false);
            merMethod->set_active(0);
        }
        if (complexsoft > 0) {
            LLmaskcolshapewav->reset();
            fftColorMask->set_active(false);
        }

        if (complexsoft == 2) {
            lapmaskcol->setValue(0);
            blurcol->setValue(0.2);
            contcol->setValue(0.);
        }

        // Exposure
        expexpose->setEnabled(pp->locallab.spots.at(index).expexpose);
        expcomp->setValue(pp->locallab.spots.at(index).expcomp);
        hlcompr->setValue(pp->locallab.spots.at(index).hlcompr);
        hlcomprthresh->setValue(pp->locallab.spots.at(index).hlcomprthresh);
        black->setValue(pp->locallab.spots.at(index).black);
        shadex->setValue(pp->locallab.spots.at(index).shadex);
        shcompr->setValue(pp->locallab.spots.at(index).shcompr);
        expchroma->setValue(pp->locallab.spots.at(index).expchroma);
        warm->setValue(pp->locallab.spots.at(index).warm);
        sensiex->setValue(pp->locallab.spots.at(index).sensiex);
        structexp->setValue(pp->locallab.spots.at(index).structexp);
        blurexpde->setValue(pp->locallab.spots.at(index).blurexpde);
        strexp->setValue(pp->locallab.spots.at(index).strexp);
        angexp->setValue(pp->locallab.spots.at(index).angexp);
        shapeexpos->setCurve(pp->locallab.spots.at(index).excurve);
        inversex->set_active(pp->locallab.spots.at(index).inversex);
        enaExpMask->set_active(pp->locallab.spots.at(index).enaExpMask);
        enaExpMaskaft->set_active(pp->locallab.spots.at(index).enaExpMaskaft);
        CCmaskexpshape->setCurve(pp->locallab.spots.at(index).CCmaskexpcurve);
        LLmaskexpshape->setCurve(pp->locallab.spots.at(index).LLmaskexpcurve);
        HHmaskexpshape->setCurve(pp->locallab.spots.at(index).HHmaskexpcurve);
        blendmaskexp->setValue(pp->locallab.spots.at(index).blendmaskexp);
        radmaskexp->setValue(pp->locallab.spots.at(index).radmaskexp);
        chromaskexp->setValue(pp->locallab.spots.at(index).chromaskexp);
        gammaskexp->setValue(pp->locallab.spots.at(index).gammaskexp);
        slomaskexp->setValue(pp->locallab.spots.at(index).slomaskexp);
        lapmaskexp->setValue(pp->locallab.spots.at(index).lapmaskexp);
        strmaskexp->setValue(pp->locallab.spots.at(index).strmaskexp);
        angmaskexp->setValue(pp->locallab.spots.at(index).angmaskexp);
        softradiusexp->setValue(pp->locallab.spots.at(index).softradiusexp);
        Lmaskexpshape->setCurve(pp->locallab.spots.at(index).Lmaskexpcurve);

        if (pp->locallab.spots.at(index).expMethod == "std") {
            expMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).expMethod == "pde") {
            expMethod->set_active(1);
        }

        if (complexsoft == 2) {
            expMethod->set_active(1);
            expcomp->setValue(0);
            hlcompr->setValue(0);
            hlcomprthresh->setValue(0);
            black->setValue(0);
            shadex->setValue(0);
            shcompr->setValue(0);
            expchroma->setValue(0);
            structexp->setValue(0);
            blurexpde->setValue(5);
            gammaskexp->setValue(1);
            slomaskexp->setValue(0);
            strmaskexp->setValue(0);
            angmaskexp->setValue(0);
            softradiusexp->setValue(0);
        }

        if (complexsoft > 0) {
            lapmaskexp->setValue(0);
        }

        if (pp->locallab.spots.at(index).exnoiseMethod == "one") {
            exnoiseMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).exnoiseMethod == "med") {
            exnoiseMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).exnoiseMethod == "medhi") {
            exnoiseMethod->set_active(2);
        }

        /*
        } else if (pp->locallab.spots.at(index).exnoiseMethod == "wavlo") {
            exnoiseMethod->set_active(3);
        } else if (pp->locallab.spots.at(index).exnoiseMethod == "wavme") {
            exnoiseMethod->set_active(4);
        } else if (pp->locallab.spots.at(index).exnoiseMethod == "wavhi") {
            exnoiseMethod->set_active(5);
        }
        */
        laplacexp->setValue(pp->locallab.spots.at(index).laplacexp);
        balanexp->setValue(pp->locallab.spots.at(index).balanexp);
        linear->setValue(pp->locallab.spots.at(index).linear);
        gamm->setValue(pp->locallab.spots.at(index).gamm);
        fatamount->setValue(pp->locallab.spots.at(index).fatamount);
        fatdetail->setValue(pp->locallab.spots.at(index).fatdetail);
        fatanchor->setValue(pp->locallab.spots.at(index).fatanchor);
        fatlevel->setValue(pp->locallab.spots.at(index).fatlevel);

        if (complexsoft > 0) {
            laplacexp->setValue(0);
            fatlevel->setValue(2);
        }

        // Shadow highlight
        expshadhigh->setEnabled(pp->locallab.spots.at(index).expshadhigh);
        highlights->setValue(pp->locallab.spots.at(index).highlights);
        h_tonalwidth->setValue(pp->locallab.spots.at(index).h_tonalwidth);
        shadows->setValue(pp->locallab.spots.at(index).shadows);
        s_tonalwidth->setValue(pp->locallab.spots.at(index).s_tonalwidth);
        sh_radius->setValue(pp->locallab.spots.at(index).sh_radius);
        sensihs->setValue(pp->locallab.spots.at(index).sensihs);
        enaSHMask->set_active(pp->locallab.spots.at(index).enaSHMask);
        CCmaskSHshape->setCurve(pp->locallab.spots.at(index).CCmaskSHcurve);
        LLmaskSHshape->setCurve(pp->locallab.spots.at(index).LLmaskSHcurve);
        HHmaskSHshape->setCurve(pp->locallab.spots.at(index).HHmaskSHcurve);
        blendmaskSH->setValue(pp->locallab.spots.at(index).blendmaskSH);
        radmaskSH->setValue(pp->locallab.spots.at(index).radmaskSH);
        blurSHde->setValue(pp->locallab.spots.at(index).blurSHde);
        strSH->setValue(pp->locallab.spots.at(index).strSH);
        angSH->setValue(pp->locallab.spots.at(index).angSH);
        inverssh->set_active(pp->locallab.spots.at(index).inverssh);
        chromaskSH->setValue(pp->locallab.spots.at(index).chromaskSH);
        gammaskSH->setValue(pp->locallab.spots.at(index).gammaskSH);
        slomaskSH->setValue(pp->locallab.spots.at(index).slomaskSH);
        lapmaskSH->setValue(pp->locallab.spots.at(index).lapmaskSH);
        detailSH->setValue(pp->locallab.spots.at(index).detailSH);
        LmaskSHshape->setCurve(pp->locallab.spots.at(index).LmaskSHcurve);

        if (pp->locallab.spots.at(index).shMethod == "std") {
            shMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).shMethod == "tone") {
            shMethod->set_active(1);
        }

        if (complexsoft == 2) {
            shMethod->set_active(1);
            highlights->setValue(0);
            shadows->setValue(0);
        }

        if (complexsoft == 2) {
            gammaskSH->setValue(1);
            slomaskSH->setValue(0);
            strSH->setValue(0);
            angSH->setValue(0);
        }

        if (complexsoft > 0) {
            lapmaskSH->setValue(0);
        }

        for (int i = 0; i < 5; i++) {
            multipliersh[i]->setValue(pp->locallab.spots.at(index).multsh[i]);
        }

        fatamountSH->setValue(pp->locallab.spots.at(index).fatamountSH);
        fatanchorSH->setValue(pp->locallab.spots.at(index).fatanchorSH);
        gamSH->setValue(pp->locallab.spots.at(index).gamSH);
        sloSH->setValue(pp->locallab.spots.at(index).sloSH);

        // Vibrance
        expvibrance->setEnabled(pp->locallab.spots.at(index).expvibrance);
        saturated->setValue(pp->locallab.spots.at(index).saturated);
        pastels->setValue(pp->locallab.spots.at(index).pastels);
        psThreshold->setValue<int>(pp->locallab.spots.at(index).psthreshold);
        protectSkins->set_active(pp->locallab.spots.at(index).protectskins);
        avoidColorShift->set_active(pp->locallab.spots.at(index).avoidcolorshift);
        pastSatTog->set_active(pp->locallab.spots.at(index).pastsattog);
        sensiv->setValue(pp->locallab.spots.at(index).sensiv);
        skinTonesCurve->setCurve(pp->locallab.spots.at(index).skintonescurve);
        CCmaskvibshape->setCurve(pp->locallab.spots.at(index).CCmaskvibcurve);
        LLmaskvibshape->setCurve(pp->locallab.spots.at(index).LLmaskvibcurve);
        HHmaskvibshape->setCurve(pp->locallab.spots.at(index).HHmaskvibcurve);
        enavibMask->set_active(pp->locallab.spots.at(index).enavibMask);
        blendmaskvib->setValue(pp->locallab.spots.at(index).blendmaskvib);
        radmaskvib->setValue(pp->locallab.spots.at(index).radmaskvib);
        chromaskvib->setValue(pp->locallab.spots.at(index).chromaskvib);
        gammaskvib->setValue(pp->locallab.spots.at(index).gammaskvib);
        slomaskvib->setValue(pp->locallab.spots.at(index).slomaskvib);
        lapmaskvib->setValue(pp->locallab.spots.at(index).lapmaskvib);
        Lmaskvibshape->setCurve(pp->locallab.spots.at(index).Lmaskvibcurve);
        strvib->setValue(pp->locallab.spots.at(index).strvib);
        strvibab->setValue(pp->locallab.spots.at(index).strvibab);
        strvibh->setValue(pp->locallab.spots.at(index).strvibh);
        angvib->setValue(pp->locallab.spots.at(index).angvib);

        if (complexsoft < 2) {
            pastels->setLabel(M("TP_VIBRANCE_PASTELS"));
        }

        if (complexsoft == 2) {
            gammaskvib->setValue(1);
            slomaskvib->setValue(0);
            skinTonesCurve->reset();
        }

        if (complexsoft > 0) {
            lapmaskvib->setValue(0);
            strvibab->setValue(0);
            strvibh->setValue(0);
        }

        // Soft Light
        expsoft->setEnabled(pp->locallab.spots.at(index).expsoft);
        streng->setValue(pp->locallab.spots.at(index).streng);
        sensisf->setValue(pp->locallab.spots.at(index).sensisf);
        laplace->setValue(pp->locallab.spots.at(index).laplace);

        if (pp->locallab.spots.at(index).softMethod == "soft") {
            softMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).softMethod == "reti") {
            softMethod->set_active(1);
        }

        if (complexsoft == 2) {
            softMethod->set_active(0);
            streng->setValue(1);
        }

        // Blur & Noise
        expblur->setEnabled(pp->locallab.spots.at(index).expblur);
        radius->setValue(pp->locallab.spots.at(index).radius);
        strength->setValue(pp->locallab.spots.at(index).strength);
        sensibn->setValue(pp->locallab.spots.at(index).sensibn);
        itera->setValue(pp->locallab.spots.at(index).itera);
        isogr->setValue(pp->locallab.spots.at(index).isogr);
        strengr->setValue(pp->locallab.spots.at(index).strengr);
        scalegr->setValue(pp->locallab.spots.at(index).scalegr);
        guidbl->setValue(pp->locallab.spots.at(index).guidbl);
        epsbl->setValue(pp->locallab.spots.at(index).epsbl);

        if (pp->locallab.spots.at(index).blMethod == "blur") {
            blMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).blMethod == "med") {
            blMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).blMethod == "guid") {
            blMethod->set_active(2);
        }

        if (complexsoft == 2) {
            blMethod->set_active(0);
            guidbl->setValue(0);
        }


        if (pp->locallab.spots.at(index).blurMethod == "norm") {
            blurMethod->set_active(0);
            //   expmaskbl->show();
        } else if (pp->locallab.spots.at(index).blurMethod == "inv") {
            blurMethod->set_active(1);
            //   expmaskbl->hide();
        }

        if (pp->locallab.spots.at(index).medMethod == "none") {
            medMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).medMethod == "33") {
            medMethod->set_active(1);
        } else if (pp->locallab.spots.at(index).medMethod == "55") {
            medMethod->set_active(2);
        } else if (pp->locallab.spots.at(index).medMethod == "77") {
            medMethod->set_active(3);
        } else if (pp->locallab.spots.at(index).medMethod == "99") {
            medMethod->set_active(4);
        }

        if (complexsoft == 2) {
            medMethod->set_active(0);
        }

        activlum->set_active(pp->locallab.spots.at(index).activlum);
        CCmaskblshape->setCurve(pp->locallab.spots.at(index).CCmaskblcurve);
        LLmaskblshape->setCurve(pp->locallab.spots.at(index).LLmaskblcurve);
        HHmaskblshape->setCurve(pp->locallab.spots.at(index).HHmaskblcurve);
        enablMask->set_active(pp->locallab.spots.at(index).enablMask);
        blendmaskbl->setValue(pp->locallab.spots.at(index).blendmaskbl);
        radmaskbl->setValue(pp->locallab.spots.at(index).radmaskbl);
        chromaskbl->setValue(pp->locallab.spots.at(index).chromaskbl);
        gammaskbl->setValue(pp->locallab.spots.at(index).gammaskbl);
        slomaskbl->setValue(pp->locallab.spots.at(index).slomaskbl);
        lapmaskbl->setValue(pp->locallab.spots.at(index).lapmaskbl);
        shadmaskbl->setValue(pp->locallab.spots.at(index).shadmaskbl);
        strumaskbl->setValue(pp->locallab.spots.at(index).strumaskbl);
        fftwbl->set_active(pp->locallab.spots.at(index).fftwbl);
        toolbl->set_active(pp->locallab.spots.at(index).toolbl);
        Lmaskblshape->setCurve(pp->locallab.spots.at(index).Lmaskblcurve);
        LLmaskblshapewav->setCurve(pp->locallab.spots.at(index).LLmaskblcurvewav);
        csThresholdblur->setValue<int>(pp->locallab.spots.at(index).csthresholdblur);

        if (complexsoft == 2) {
            gammaskbl->setValue(1);
            slomaskbl->setValue(0);
            LLmaskblshapewav->reset();
            strumaskbl->setValue(0);
            shadmaskbl->setValue(0);
        }

        if (complexsoft > 0) {
            lapmaskbl->setValue(0);
            LLmaskblshapewav->reset();
        }

        // Tone Mapping
        exptonemap->setEnabled(pp->locallab.spots.at(index).exptonemap);
        stren->setValue(pp->locallab.spots.at(index).stren);
        gamma->setValue(pp->locallab.spots.at(index).gamma);
        estop->setValue(pp->locallab.spots.at(index).estop);
        scaltm->setValue(pp->locallab.spots.at(index).scaltm);
        rewei->setValue(pp->locallab.spots.at(index).rewei);
        satur->setValue(pp->locallab.spots.at(index).satur);
        sensitm->setValue(pp->locallab.spots.at(index).sensitm);
        softradiustm->setValue(pp->locallab.spots.at(index).softradiustm);
        amount->setValue(pp->locallab.spots.at(index).amount);
        equiltm->set_active(pp->locallab.spots.at(index).equiltm);
        enatmMask->set_active(pp->locallab.spots.at(index).enatmMask);
        enatmMaskaft->set_active(pp->locallab.spots.at(index).enatmMaskaft);
        blendmasktm->setValue(pp->locallab.spots.at(index).blendmasktm);
        radmasktm->setValue(pp->locallab.spots.at(index).radmasktm);
        chromasktm->setValue(pp->locallab.spots.at(index).chromasktm);
        gammasktm->setValue(pp->locallab.spots.at(index).gammasktm);
        slomasktm->setValue(pp->locallab.spots.at(index).slomasktm);
        lapmasktm->setValue(pp->locallab.spots.at(index).lapmasktm);
        CCmasktmshape->setCurve(pp->locallab.spots.at(index).CCmasktmcurve);
        LLmasktmshape->setCurve(pp->locallab.spots.at(index).LLmasktmcurve);
        HHmasktmshape->setCurve(pp->locallab.spots.at(index).HHmasktmcurve);
        Lmasktmshape->setCurve(pp->locallab.spots.at(index).Lmasktmcurve);

        if (complexsoft == 2) {
            gammasktm->setValue(1);
            slomasktm->setValue(0);
            gamma->setValue(1);
            satur->setValue(0);
            rewei->setValue(0);
        }

        if (complexsoft > 0) {
            lapmasktm->setValue(0);
        }

        // Retinex
        expreti->setEnabled(pp->locallab.spots.at(index).expreti);

        if (pp->locallab.spots.at(index).retinexMethod == "low") {
            retinexMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).retinexMethod == "uni") {
            retinexMethod->set_active(1);
        } else {
            retinexMethod->set_active(2);
        }

        str->setValue(pp->locallab.spots.at(index).str);

        if (complexsoft == 2) {
            str->setValue(0);
        }

        chrrt->setValue(pp->locallab.spots.at(index).chrrt);
        neigh->setValue(pp->locallab.spots.at(index).neigh);
        vart->setValue(pp->locallab.spots.at(index).vart);
        offs->setValue(pp->locallab.spots.at(index).offs);
        dehaz->setValue(pp->locallab.spots.at(index).dehaz);
        depth->setValue(pp->locallab.spots.at(index).depth);
        sensih->setValue(pp->locallab.spots.at(index).sensih);
        cTgainshape->setCurve(pp->locallab.spots.at(index).localTgaincurve);
        cTtransshape->setCurve(pp->locallab.spots.at(index).localTtranscurve);
        inversret->set_active(pp->locallab.spots.at(index).inversret);
        equilret->set_active(pp->locallab.spots.at(index).equilret);
        loglin->set_active(pp->locallab.spots.at(index).loglin);
        lumonly->set_active(pp->locallab.spots.at(index).lumonly);
        softradiusret->setValue(pp->locallab.spots.at(index).softradiusret);
        CCmaskretishape->setCurve(pp->locallab.spots.at(index).CCmaskreticurve);
        LLmaskretishape->setCurve(pp->locallab.spots.at(index).LLmaskreticurve);
        HHmaskretishape->setCurve(pp->locallab.spots.at(index).HHmaskreticurve);
        enaretiMask->set_active(pp->locallab.spots.at(index).enaretiMask);
        enaretiMasktmap->set_active(pp->locallab.spots.at(index).enaretiMasktmap);
        blendmaskreti->setValue(pp->locallab.spots.at(index).blendmaskreti);
        radmaskreti->setValue(pp->locallab.spots.at(index).radmaskreti);
        chromaskreti->setValue(pp->locallab.spots.at(index).chromaskreti);
        gammaskreti->setValue(pp->locallab.spots.at(index).gammaskreti);
        slomaskreti->setValue(pp->locallab.spots.at(index).slomaskreti);
        lapmaskreti->setValue(pp->locallab.spots.at(index).lapmaskreti);
        scalereti->setValue(pp->locallab.spots.at(index).scalereti);
        darkness->setValue(pp->locallab.spots.at(index).darkness);
        lightnessreti->setValue(pp->locallab.spots.at(index).lightnessreti);
        limd->setValue(pp->locallab.spots.at(index).limd);
        cliptm->setValue(pp->locallab.spots.at(index).cliptm);
        fftwreti->set_active(pp->locallab.spots.at(index).fftwreti);
        Lmaskretishape->setCurve(pp->locallab.spots.at(index).Lmaskreticurve);

        // Sharpening
        expsharp->setEnabled(pp->locallab.spots.at(index).expsharp);
        sharcontrast->setValue(pp->locallab.spots.at(index).sharcontrast);
        sharradius->setValue(pp->locallab.spots.at(index).sharradius);
        sharamount->setValue(pp->locallab.spots.at(index).sharamount);
        shardamping->setValue(pp->locallab.spots.at(index).shardamping);
        shariter->setValue(pp->locallab.spots.at(index).shariter);
        sharblur->setValue(pp->locallab.spots.at(index).sharblur);
        sensisha->setValue(pp->locallab.spots.at(index).sensisha);
        inverssha->set_active(pp->locallab.spots.at(index).inverssha);

        if (complexsoft == 2) {
            shardamping->setValue(0);
            shariter->setValue(30);
            sharblur->setValue(0.2);
        }

        // Local Contrast
        expcontrast->setEnabled(pp->locallab.spots.at(index).expcontrast);
        lcradius->setValue(pp->locallab.spots.at(index).lcradius);
        lcamount->setValue(pp->locallab.spots.at(index).lcamount);
        lcdarkness->setValue(pp->locallab.spots.at(index).lcdarkness);
        lclightness->setValue(pp->locallab.spots.at(index).lclightness);
        levelwav->setValue(pp->locallab.spots.at(index).levelwav);
        residcont->setValue(pp->locallab.spots.at(index).residcont);
        residchro->setValue(pp->locallab.spots.at(index).residchro);
        clarilres->setValue(pp->locallab.spots.at(index).clarilres);
        claricres->setValue(pp->locallab.spots.at(index).claricres);
        clarisoft->setValue(pp->locallab.spots.at(index).clarisoft);
        sensilc->setValue(pp->locallab.spots.at(index).sensilc);
        fftwlc->set_active(pp->locallab.spots.at(index).fftwlc);
        csThreshold->setValue<int>(pp->locallab.spots.at(index).csthreshold);

        if (pp->locallab.spots.at(index).localcontMethod == "loc") {
            localcontMethod->set_active(0);
        } else if (pp->locallab.spots.at(index).localcontMethod == "wav") {
            localcontMethod->set_active(1);
        }

        if (complexsoft == 2) {
            localcontMethod->set_active(1);
            claricres->setValue(0);
            clarisoft->setValue(0);
            lcradius->setValue(80);
            lcamount->setValue(0);
            residchro->setValue(0);
            fftwlc->set_active(false);
        }

        wavshape->setCurve(pp->locallab.spots.at(index).locwavcurve);

        // Contrast by detail levels
        expcbdl->setEnabled(pp->locallab.spots.at(index).expcbdl);

        for (int i = 0; i < 6; i++) {
            multiplier[i]->setValue(pp->locallab.spots.at(index).mult[i]);
        }

        chromacbdl->setValue(pp->locallab.spots.at(index).chromacbdl);
        threshold->setValue(pp->locallab.spots.at(index).threshold);
        sensicb->setValue(pp->locallab.spots.at(index).sensicb);
        clarityml->setValue(pp->locallab.spots.at(index).clarityml);
        contresid->setValue(pp->locallab.spots.at(index).contresid);
        blurcbdl->setValue(pp->locallab.spots.at(index).blurcbdl);
        softradiuscb->setValue(pp->locallab.spots.at(index).softradiuscb);
        blendmaskcb->setValue(pp->locallab.spots.at(index).blendmaskcb);
        radmaskcb->setValue(pp->locallab.spots.at(index).radmaskcb);
        chromaskcb->setValue(pp->locallab.spots.at(index).chromaskcb);
        gammaskcb->setValue(pp->locallab.spots.at(index).gammaskcb);
        slomaskcb->setValue(pp->locallab.spots.at(index).slomaskcb);
        lapmaskcb->setValue(pp->locallab.spots.at(index).lapmaskcb);
        enacbMask->set_active(pp->locallab.spots.at(index).enacbMask);
        CCmaskcbshape->setCurve(pp->locallab.spots.at(index).CCmaskcbcurve);
        LLmaskcbshape->setCurve(pp->locallab.spots.at(index).LLmaskcbcurve);
        HHmaskcbshape->setCurve(pp->locallab.spots.at(index).HHmaskcbcurve);
        Lmaskcbshape->setCurve(pp->locallab.spots.at(index).Lmaskcbcurve);

        if (complexsoft == 2) {
            for (int i = 0; i < 6; i++) {
                multiplier[i]->setValue(1.0);
            }
        }

        if (complexsoft > 0) {
            lapmaskcb->setValue(0);
        }

        // Denoise
        expdenoi->setEnabled(pp->locallab.spots.at(index).expdenoi);
        noiselumf->setValue(pp->locallab.spots.at(index).noiselumf);
        noiselumf0->setValue(pp->locallab.spots.at(index).noiselumf0);
        noiselumf2->setValue(pp->locallab.spots.at(index).noiselumf2);
        noiselumc->setValue(pp->locallab.spots.at(index).noiselumc);
        noiselumdetail->setValue(pp->locallab.spots.at(index).noiselumdetail);
        noiselequal->setValue(pp->locallab.spots.at(index).noiselequal);
        noisechrof->setValue(pp->locallab.spots.at(index).noisechrof);
        noisechroc->setValue(pp->locallab.spots.at(index).noisechroc);
        noisechrodetail->setValue(pp->locallab.spots.at(index).noisechrodetail);
        adjblur->setValue(pp->locallab.spots.at(index).adjblur);
        bilateral->setValue(pp->locallab.spots.at(index).bilateral);
        sensiden->setValue(pp->locallab.spots.at(index).sensiden);
        detailthr->setValue(pp->locallab.spots.at(index).detailthr);

        if (complexsoft == 2) {
            noiselumf->setValue(0);
            noiselumf0->setValue(0);
            noiselumf2->setValue(0);
            noiselumc->setValue(0);
            noisechrof->setValue(0);
            noisechroc->setValue(0);
            bilateral->setValue(0);
        }

        if (pedited) {
            if (index < (int)pedited->locallab.spots.size()) {
                const LocallabParamsEdited::LocallabSpotEdited* spotState = &pedited->locallab.spots.at(index);

                // Control spot settings
                ControlSpotPanel::SpotEdited* const se = new ControlSpotPanel::SpotEdited();

                if (pedited->locallab.nbspot && pedited->locallab.id) {
                    se->nbspot = true;
                } else {
                    se->nbspot = false;
                }

                se->selspot = pedited->locallab.selspot;
                se->name = spotState->name;
                se->isvisible = spotState->isvisible;
                se->shape = spotState->shape;
                se->spotMethod = spotState->spotMethod;
//               se->mergeMethod = spotState->mergeMethod;
                se->sensiexclu = spotState->sensiexclu;
                se->structexclu = spotState->structexclu;
                se->struc = spotState->struc;
                se->shapeMethod = spotState->shapeMethod;
                se->locX = spotState->locX;
                se->locXL = spotState->locXL;
                se->locY = spotState->locY;
                se->locYT = spotState->locYT;
                se->centerX = spotState->centerX;
                se->centerY = spotState->centerY;
                se->circrad = spotState->circrad;
                se->qualityMethod = spotState->qualityMethod;
                se->complexMethod = spotState->complexMethod;
                se->transit = spotState->transit;
                se->feather = spotState->feather;
                se->thresh = spotState->thresh;
                se->iter = spotState->iter;
                se->balan = spotState->balan;
                se->transitweak = spotState->transitweak;
                se->transitgrad = spotState->transitgrad;
                se->scopemask = spotState->scopemask;
                se->lumask = spotState->lumask;
                se->avoid = spotState->avoid;
                se->recurs = spotState->recurs;
                se->laplac = spotState->laplac;
                se->deltae = spotState->deltae;
                se->shortc = spotState->shortc;
                se->savrest = spotState->savrest;
                expsettings->setEditedStates(se);

                // Color & Light
                expcolor->set_inconsistent(!spotState->expcolor);
                curvactiv->set_inconsistent(multiImage && !spotState->curvactiv);
                lightness->setEditedState(spotState->lightness ? Edited : UnEdited);
                contrast->setEditedState(spotState->contrast ? Edited : UnEdited);
                chroma->setEditedState(spotState->chroma ? Edited : UnEdited);
                sensi->setEditedState(spotState->sensi ? Edited : UnEdited);
                structcol->setEditedState(spotState->structcol ? Edited : UnEdited);
                strcol->setEditedState(spotState->strcol ? Edited : UnEdited);
                strcolab->setEditedState(spotState->strcolab ? Edited : UnEdited);
                strcolh->setEditedState(spotState->strcolh ? Edited : UnEdited);
                angcol->setEditedState(spotState->angcol ? Edited : UnEdited);
                labgrid->setEdited(spotState->labgridALow || spotState->labgridBLow || spotState->labgridAHigh || spotState->labgridBHigh);
                labgridmerg->setEdited(spotState->labgridALowmerg || spotState->labgridBLowmerg || spotState->labgridAHighmerg || spotState->labgridBHighmerg);
                strengthgrid->setEditedState(spotState->strengthgrid ? Edited : UnEdited);

                if (!spotState->qualitycurveMethod) {
                    qualitycurveMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->gridMethod) {
                    gridMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->merMethod) {
                    merMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->toneMethod) {
                    toneMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->mergecolMethod) {
                    mergecolMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                llshape->setUnChanged(!spotState->llcurve);
                lcshape->setUnChanged(!spotState->lccurve);
                ccshape->setUnChanged(!spotState->cccurve);
                clshape->setUnChanged(!spotState->clcurve);
                rgbshape->setUnChanged(!spotState->rgbcurve);
                LHshape->setUnChanged(!spotState->LHcurve);
                HHshape->setUnChanged(!spotState->HHcurve);
                invers->set_inconsistent(multiImage && !spotState->invers);
                special->set_inconsistent(multiImage && !spotState->special);
                toolcol->set_inconsistent(multiImage && !spotState->toolcol);
                enaColorMask->set_inconsistent(multiImage && !spotState->enaColorMask);
                fftColorMask->set_inconsistent(multiImage && !spotState->fftColorMask);
                CCmaskshape->setUnChanged(!spotState->CCmaskcurve);
                LLmaskshape->setUnChanged(!spotState->LLmaskcurve);
                HHmaskshape->setUnChanged(!spotState->HHmaskcurve);
                HHhmaskshape->setUnChanged(!spotState->HHhmaskcurve);
                blurcolde->setEditedState(spotState->blurcolde ? Edited : UnEdited);
                blurcol->setEditedState(spotState->blurcol ? Edited : UnEdited);
                contcol->setEditedState(spotState->contcol ? Edited : UnEdited);
                blendmaskcol->setEditedState(spotState->blendmaskcol ? Edited : UnEdited);
                radmaskcol->setEditedState(spotState->radmaskcol ? Edited : UnEdited);
                chromaskcol->setEditedState(spotState->chromaskcol ? Edited : UnEdited);
                gammaskcol->setEditedState(spotState->gammaskcol ? Edited : UnEdited);
                slomaskcol->setEditedState(spotState->slomaskcol ? Edited : UnEdited);
                shadmaskcol->setEditedState(spotState->shadmaskcol ? Edited : UnEdited);
                strumaskcol->setEditedState(spotState->strumaskcol ? Edited : UnEdited);
                lapmaskcol->setEditedState(spotState->lapmaskcol ? Edited : UnEdited);
                softradiuscol->setEditedState(spotState->softradiuscol ? Edited : UnEdited);
                opacol->setEditedState(spotState->opacol ? Edited : UnEdited);
                mercol->setEditedState(spotState->mercol ? Edited : UnEdited);
                merlucol->setEditedState(spotState->merlucol ? Edited : UnEdited);
                conthrcol->setEditedState(spotState->conthrcol ? Edited : UnEdited);
                Lmaskshape->setUnChanged(!spotState->Lmaskcurve);
                LLmaskcolshapewav->setUnChanged(!spotState->LLmaskcolcurvewav);
                csThresholdcol->setEditedState(spotState->csthresholdcol ? Edited : UnEdited);

                // Exposure
                expexpose->set_inconsistent(!spotState->expexpose);
                expcomp->setEditedState(spotState->expcomp ? Edited : UnEdited);
                hlcompr->setEditedState(spotState->hlcompr ? Edited : UnEdited);
                hlcomprthresh->setEditedState(spotState->hlcomprthresh ? Edited : UnEdited);
                black->setEditedState(spotState->black ? Edited : UnEdited);
                shadex->setEditedState(spotState->shadex ? Edited : UnEdited);
                warm->setEditedState(spotState->warm ? Edited : UnEdited);
                shcompr->setEditedState(spotState->shcompr ? Edited : UnEdited);
                expchroma->setEditedState(spotState->expchroma ? Edited : UnEdited);
                sensiex->setEditedState(spotState->sensiex ? Edited : UnEdited);
                structexp->setEditedState(spotState->structexp ? Edited : UnEdited);
                blurexpde->setEditedState(spotState->blurexpde ? Edited : UnEdited);
                strexp->setEditedState(spotState->strexp ? Edited : UnEdited);
                angexp->setEditedState(spotState->angexp ? Edited : UnEdited);
                shapeexpos->setUnChanged(!spotState->excurve);
                inversex->set_inconsistent(multiImage && !spotState->inversex);
                enaExpMask->set_inconsistent(multiImage && !spotState->enaExpMask);
                enaExpMaskaft->set_inconsistent(multiImage && !spotState->enaExpMaskaft);
                CCmaskexpshape->setUnChanged(!spotState->CCmaskexpcurve);
                LLmaskexpshape->setUnChanged(!spotState->LLmaskexpcurve);
                HHmaskexpshape->setUnChanged(!spotState->HHmaskexpcurve);
                blendmaskexp->setEditedState(spotState->blendmaskexp ? Edited : UnEdited);
                radmaskexp->setEditedState(spotState->radmaskexp ? Edited : UnEdited);
                chromaskexp->setEditedState(spotState->chromaskexp ? Edited : UnEdited);
                gammaskexp->setEditedState(spotState->gammaskexp ? Edited : UnEdited);
                slomaskexp->setEditedState(spotState->slomaskexp ? Edited : UnEdited);
                lapmaskexp->setEditedState(spotState->lapmaskexp ? Edited : UnEdited);
                strmaskexp->setEditedState(spotState->strmaskexp ? Edited : UnEdited);
                angmaskexp->setEditedState(spotState->angmaskexp ? Edited : UnEdited);
                softradiusexp->setEditedState(spotState->softradiusexp ? Edited : UnEdited);
                Lmaskexpshape->setUnChanged(!spotState->Lmaskexpcurve);

                if (!spotState->expMethod) {
                    expMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->exnoiseMethod) {
                    exnoiseMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                laplacexp->setEditedState(spotState->laplacexp ? Edited : UnEdited);
                balanexp->setEditedState(spotState->balanexp ? Edited : UnEdited);
                linear->setEditedState(spotState->linear ? Edited : UnEdited);
                gamm->setEditedState(spotState->gamm ? Edited : UnEdited);
                fatamount->setEditedState(spotState->fatamount ? Edited : UnEdited);
                fatdetail->setEditedState(spotState->fatdetail ? Edited : UnEdited);
                fatanchor->setEditedState(spotState->fatanchor ? Edited : UnEdited);
                fatlevel->setEditedState(spotState->fatlevel ? Edited : UnEdited);

                // Shadow highlight
                expshadhigh->set_inconsistent(!spotState->expshadhigh);
                highlights->setEditedState(spotState->highlights ? Edited : UnEdited);
                h_tonalwidth->setEditedState(spotState->h_tonalwidth ? Edited : UnEdited);
                shadows->setEditedState(spotState->shadows ? Edited : UnEdited);
                s_tonalwidth->setEditedState(spotState->s_tonalwidth ? Edited : UnEdited);
                sh_radius->setEditedState(spotState->sh_radius ? Edited : UnEdited);
                sensihs->setEditedState(spotState->sensihs ? Edited : UnEdited);
                enaSHMask->set_inconsistent(multiImage && !spotState->enaSHMask);
                CCmaskSHshape->setUnChanged(!spotState->CCmaskSHcurve);
                LLmaskSHshape->setUnChanged(!spotState->LLmaskSHcurve);
                HHmaskSHshape->setUnChanged(!spotState->HHmaskSHcurve);
                blendmaskSH->setEditedState(spotState->blendmaskSH ? Edited : UnEdited);
                radmaskSH->setEditedState(spotState->radmaskSH ? Edited : UnEdited);
                blurSHde->setEditedState(spotState->blurSHde ? Edited : UnEdited);
                strSH->setEditedState(spotState->strSH ? Edited : UnEdited);
                angSH->setEditedState(spotState->angSH ? Edited : UnEdited);
                inverssh->set_inconsistent(multiImage && !spotState->inverssh);
                chromaskSH->setEditedState(spotState->chromaskSH ? Edited : UnEdited);
                gammaskSH->setEditedState(spotState->gammaskSH ? Edited : UnEdited);
                slomaskSH->setEditedState(spotState->slomaskSH ? Edited : UnEdited);
                lapmaskSH->setEditedState(spotState->lapmaskSH ? Edited : UnEdited);
                detailSH->setEditedState(spotState->detailSH ? Edited : UnEdited);
                LmaskSHshape->setUnChanged(!spotState->LmaskSHcurve);

                if (!spotState->shMethod) {
                    shMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                for (int i = 0; i < 5; i++) {
                    multipliersh[i]->setEditedState(spotState->multsh[i] ? Edited : UnEdited);
                }

                fatamountSH->setEditedState(spotState->fatamountSH ? Edited : UnEdited);
                fatanchorSH->setEditedState(spotState->fatanchorSH ? Edited : UnEdited);
                gamSH->setEditedState(spotState->gamSH ? Edited : UnEdited);
                sloSH->setEditedState(spotState->sloSH ? Edited : UnEdited);

                // Vibrance
                expvibrance->set_inconsistent(!spotState->expvibrance);
                saturated->setEditedState(spotState->saturated ? Edited : UnEdited);
                pastels->setEditedState(spotState->pastels ? Edited : UnEdited);
                psThreshold->setEditedState(spotState->psthreshold ? Edited : UnEdited);
                protectSkins->set_inconsistent(multiImage && !spotState->protectskins);
                avoidColorShift->set_inconsistent(multiImage && !spotState->avoidcolorshift);
                pastSatTog->set_inconsistent(multiImage && !spotState->pastsattog);
                sensiv->setEditedState(spotState->sensiv ? Edited : UnEdited);
                skinTonesCurve->setUnChanged(!spotState->skintonescurve);
                CCmaskvibshape->setUnChanged(!spotState->CCmaskvibcurve);
                LLmaskvibshape->setUnChanged(!spotState->LLmaskvibcurve);
                HHmaskvibshape->setUnChanged(!spotState->HHmaskvibcurve);
                enavibMask->set_inconsistent(multiImage && !spotState->enavibMask);
                blendmaskvib->setEditedState(spotState->blendmaskvib ? Edited : UnEdited);
                radmaskvib->setEditedState(spotState->radmaskvib ? Edited : UnEdited);
                chromaskvib->setEditedState(spotState->chromaskvib ? Edited : UnEdited);
                gammaskvib->setEditedState(spotState->gammaskvib ? Edited : UnEdited);
                slomaskvib->setEditedState(spotState->slomaskvib ? Edited : UnEdited);
                lapmaskvib->setEditedState(spotState->lapmaskvib ? Edited : UnEdited);
                Lmaskvibshape->setUnChanged(!spotState->Lmaskvibcurve);
                strvib->setEditedState(spotState->strvib ? Edited : UnEdited);
                strvibab->setEditedState(spotState->strvibab ? Edited : UnEdited);
                strvibh->setEditedState(spotState->strvibh ? Edited : UnEdited);
                angvib->setEditedState(spotState->angvib ? Edited : UnEdited);

                // Soft Light
                expsoft->set_inconsistent(!spotState->expsoft);
                streng->setEditedState(spotState->streng ? Edited : UnEdited);
                sensisf->setEditedState(spotState->sensisf ? Edited : UnEdited);
                laplace->setEditedState(spotState->laplace ? Edited : UnEdited);

                if (!spotState->softMethod) {
                    softMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                // Blur & Noise
                expblur->set_inconsistent(!spotState->expblur);
                radius->setEditedState(spotState->radius ? Edited : UnEdited);
                strength->setEditedState(spotState->strength ? Edited : UnEdited);
                sensibn->setEditedState(spotState->sensibn ? Edited : UnEdited);
                itera->setEditedState(spotState->itera ? Edited : UnEdited);
                isogr->setEditedState(spotState->isogr ? Edited : UnEdited);
                strengr->setEditedState(spotState->strengr ? Edited : UnEdited);
                scalegr->setEditedState(spotState->scalegr ? Edited : UnEdited);
                guidbl->setEditedState(spotState->guidbl ? Edited : UnEdited);
                epsbl->setEditedState(spotState->epsbl ? Edited : UnEdited);

                if (!spotState->blMethod) {
                    blMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->blurMethod) {
                    blurMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                if (!spotState->medMethod) {
                    medMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                activlum->set_inconsistent(multiImage && !spotState->activlum);
                CCmaskblshape->setUnChanged(!spotState->CCmaskblcurve);
                LLmaskblshape->setUnChanged(!spotState->LLmaskblcurve);
                HHmaskblshape->setUnChanged(!spotState->HHmaskblcurve);
                enablMask->set_inconsistent(multiImage && !spotState->enablMask);
                blendmaskbl->setEditedState(spotState->blendmaskbl ? Edited : UnEdited);
                radmaskbl->setEditedState(spotState->radmaskbl ? Edited : UnEdited);
                chromaskbl->setEditedState(spotState->chromaskbl ? Edited : UnEdited);
                gammaskbl->setEditedState(spotState->gammaskbl ? Edited : UnEdited);
                slomaskbl->setEditedState(spotState->slomaskbl ? Edited : UnEdited);
                lapmaskbl->setEditedState(spotState->lapmaskbl ? Edited : UnEdited);
                shadmaskbl->setEditedState(spotState->shadmaskbl ? Edited : UnEdited);
                strumaskbl->setEditedState(spotState->strumaskbl ? Edited : UnEdited);
                fftwbl->set_inconsistent(multiImage && !spotState->fftwbl);
                toolbl->set_inconsistent(multiImage && !spotState->toolbl);
                Lmaskblshape->setUnChanged(!spotState->Lmaskblcurve);
                LLmaskblshapewav->setUnChanged(!spotState->LLmaskblcurvewav);
                csThresholdblur->setEditedState(spotState->csthresholdblur ? Edited : UnEdited);

                // Tone Mapping
                exptonemap->set_inconsistent(!spotState->exptonemap);
                stren->setEditedState(spotState->stren ? Edited : UnEdited);
                gamma->setEditedState(spotState->gamma ? Edited : UnEdited);
                estop->setEditedState(spotState->estop ? Edited : UnEdited);
                scaltm->setEditedState(spotState->scaltm ? Edited : UnEdited);
                rewei->setEditedState(spotState->rewei ? Edited : UnEdited);
                satur->setEditedState(spotState->satur ? Edited : UnEdited);
                sensitm->setEditedState(spotState->sensitm ? Edited : UnEdited);
                softradiustm->setEditedState(spotState->softradiustm ? Edited : UnEdited);
                amount->setEditedState(spotState->amount ? Edited : UnEdited);
                equiltm->set_inconsistent(multiImage && !spotState->equiltm);
                CCmasktmshape->setUnChanged(!spotState->CCmasktmcurve);
                LLmasktmshape->setUnChanged(!spotState->LLmasktmcurve);
                HHmasktmshape->setUnChanged(!spotState->HHmasktmcurve);
                enatmMask->set_inconsistent(multiImage && !spotState->enatmMask);
                enatmMaskaft->set_inconsistent(multiImage && !spotState->enatmMaskaft);
                blendmasktm->setEditedState(spotState->blendmasktm ? Edited : UnEdited);
                radmasktm->setEditedState(spotState->radmasktm ? Edited : UnEdited);
                chromasktm->setEditedState(spotState->chromasktm ? Edited : UnEdited);
                gammasktm->setEditedState(spotState->gammasktm ? Edited : UnEdited);
                slomasktm->setEditedState(spotState->slomasktm ? Edited : UnEdited);
                lapmasktm->setEditedState(spotState->lapmasktm ? Edited : UnEdited);
                Lmasktmshape->setUnChanged(!spotState->Lmasktmcurve);

                // Retinex
                expreti->set_inconsistent(!spotState->expreti);

                if (!spotState->retinexMethod) {
                    retinexMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                str->setEditedState(spotState->str ? Edited : UnEdited);
                chrrt->setEditedState(spotState->chrrt ? Edited : UnEdited);
                neigh->setEditedState(spotState->neigh ? Edited : UnEdited);
                vart->setEditedState(spotState->vart ? Edited : UnEdited);
                offs->setEditedState(spotState->offs ? Edited : UnEdited);
                dehaz->setEditedState(spotState->dehaz ? Edited : UnEdited);
                depth->setEditedState(spotState->depth ? Edited : UnEdited);
                sensih->setEditedState(spotState->sensih ? Edited : UnEdited);
                cTgainshape->setUnChanged(!spotState->localTgaincurve);
                cTtransshape->setUnChanged(!spotState->localTtranscurve);
                inversret->set_inconsistent(multiImage && !spotState->inversret);
                equilret->set_inconsistent(multiImage && !spotState->equilret);
                loglin->set_inconsistent(multiImage && !spotState->loglin);
                lumonly->set_inconsistent(multiImage && !spotState->lumonly);
                softradiusret->setEditedState(spotState->softradiusret ? Edited : UnEdited);
                CCmaskretishape->setUnChanged(!spotState->CCmaskreticurve);
                LLmaskretishape->setUnChanged(!spotState->LLmaskreticurve);
                HHmaskretishape->setUnChanged(!spotState->HHmaskreticurve);
                enaretiMask->set_inconsistent(multiImage && !spotState->enaretiMask);
                enaretiMasktmap->set_inconsistent(multiImage && !spotState->enaretiMasktmap);
                blendmaskreti->setEditedState(spotState->blendmaskreti ? Edited : UnEdited);
                radmaskreti->setEditedState(spotState->radmaskreti ? Edited : UnEdited);
                chromaskreti->setEditedState(spotState->chromaskreti ? Edited : UnEdited);
                gammaskreti->setEditedState(spotState->gammaskreti ? Edited : UnEdited);
                slomaskreti->setEditedState(spotState->slomaskreti ? Edited : UnEdited);
                lapmaskreti->setEditedState(spotState->lapmaskreti ? Edited : UnEdited);
                scalereti->setEditedState(spotState->scalereti ? Edited : UnEdited);
                darkness->setEditedState(spotState->darkness ? Edited : UnEdited);
                lightnessreti->setEditedState(spotState->lightnessreti ? Edited : UnEdited);
                limd->setEditedState(spotState->limd ? Edited : UnEdited);
                cliptm->setEditedState(spotState->cliptm ? Edited : UnEdited);
                fftwreti->set_inconsistent(multiImage && !spotState->fftwreti);
                Lmaskretishape->setUnChanged(!spotState->Lmaskreticurve);

                // Sharpening
                expsharp->set_inconsistent(!spotState->expsharp);
                sharcontrast->setEditedState(spotState->sharcontrast ? Edited : UnEdited);
                sharradius->setEditedState(spotState->sharradius ? Edited : UnEdited);
                sharamount->setEditedState(spotState->sharamount ? Edited : UnEdited);
                shardamping->setEditedState(spotState->shardamping ? Edited : UnEdited);
                shariter->setEditedState(spotState->shariter ? Edited : UnEdited);
                sharblur->setEditedState(spotState->sharblur ? Edited : UnEdited);
                sensisha->setEditedState(spotState->sensisha ? Edited : UnEdited);
                inverssha->set_inconsistent(multiImage && !spotState->inverssha);

                // Local Contrast
                expcontrast->set_inconsistent(!spotState->expcontrast);
                lcradius->setEditedState(spotState->lcradius ? Edited : UnEdited);
                lcamount->setEditedState(spotState->lcamount ? Edited : UnEdited);
                lcdarkness->setEditedState(spotState->lcdarkness ? Edited : UnEdited);
                lclightness->setEditedState(spotState->lclightness ? Edited : UnEdited);
                levelwav->setEditedState(spotState->levelwav ? Edited : UnEdited);
                residcont->setEditedState(spotState->residcont ? Edited : UnEdited);
                residchro->setEditedState(spotState->residchro ? Edited : UnEdited);
                clarilres->setEditedState(spotState->clarilres ? Edited : UnEdited);
                claricres->setEditedState(spotState->claricres ? Edited : UnEdited);
                clarisoft->setEditedState(spotState->clarisoft ? Edited : UnEdited);
                sensilc->setEditedState(spotState->sensilc ? Edited : UnEdited);
                fftwlc->set_inconsistent(multiImage && !spotState->fftwlc);
                wavshape->setUnChanged(!spotState->locwavcurve);
                csThreshold->setEditedState(spotState->csthreshold ? Edited : UnEdited);

                if (!spotState->retinexMethod) {
                    localcontMethod->set_active_text(M("GENERAL_UNCHANGED"));
                }

                // Contrast by detail levels
                expcbdl->set_inconsistent(!spotState->expcbdl);

                for (int i = 0; i < 6; i++) {
                    multiplier[i]->setEditedState(spotState->mult[i] ? Edited : UnEdited);
                }

                chromacbdl->setEditedState(spotState->chromacbdl ? Edited : UnEdited);
                threshold->setEditedState(spotState->threshold ? Edited : UnEdited);
                sensicb->setEditedState(spotState->sensicb ? Edited : UnEdited);
                clarityml->setEditedState(spotState->clarityml ? Edited : UnEdited);
                contresid->setEditedState(spotState->contresid ? Edited : UnEdited);
                blurcbdl->setEditedState(spotState->blurcbdl ? Edited : UnEdited);
                softradiuscb->setEditedState(spotState->softradiuscb ? Edited : UnEdited);
                blendmaskcb->setEditedState(spotState->blendmaskcb ? Edited : UnEdited);
                radmaskcb->setEditedState(spotState->radmaskcb ? Edited : UnEdited);
                chromaskcb->setEditedState(spotState->chromaskcb ? Edited : UnEdited);
                gammaskcb->setEditedState(spotState->gammaskcb ? Edited : UnEdited);
                slomaskcb->setEditedState(spotState->slomaskcb ? Edited : UnEdited);
                lapmaskcb->setEditedState(spotState->lapmaskcb ? Edited : UnEdited);
                enacbMask->set_inconsistent(multiImage && !spotState->enacbMask);
                CCmaskcbshape->setUnChanged(!spotState->CCmaskcbcurve);
                LLmaskcbshape->setUnChanged(!spotState->LLmaskcbcurve);
                HHmaskcbshape->setUnChanged(!spotState->HHmaskcbcurve);
                Lmaskcbshape->setUnChanged(!spotState->Lmaskcbcurve);

                // Denoise
                expdenoi->set_inconsistent(!spotState->expdenoi);
                noiselumf->setEditedState(spotState->noiselumf ? Edited : UnEdited);
                noiselumf0->setEditedState(spotState->noiselumf0 ? Edited : UnEdited);
                noiselumf2->setEditedState(spotState->noiselumf2 ? Edited : UnEdited);
                noiselumc->setEditedState(spotState->noiselumc ? Edited : UnEdited);
                noiselumdetail->setEditedState(spotState->noiselumdetail ? Edited : UnEdited);
                noiselequal->setEditedState(spotState->noiselequal ? Edited : UnEdited);
                noisechrof->setEditedState(spotState->noisechrof ? Edited : UnEdited);
                noisechroc->setEditedState(spotState->noisechroc ? Edited : UnEdited);
                noisechrodetail->setEditedState(spotState->noisechrodetail ? Edited : UnEdited);
                adjblur->setEditedState(spotState->adjblur ? Edited : UnEdited);
                bilateral->setEditedState(spotState->bilateral ? Edited : UnEdited);
                sensiden->setEditedState(spotState->sensiden ? Edited : UnEdited);
                detailthr->setEditedState(spotState->detailthr ? Edited : UnEdited);
            }
        }
    }
}

void Locallab::updateSpecificGUIState()
{
    // Update Color & Light GUI according to invers button state (to be compliant with inversChanged function)
    sensi->show();
    curvactiv->hide();
    blurcolde->show();

    if (multiImage && invers->get_inconsistent()) {
        llCurveEditorG->show();
        clCurveEditorG->show();
        blurcol->show();
        contcol->show();
        HCurveEditorG->show();
        qualitycurveMethod->show();
        labqualcurv->show();
        expmaskcol->show();
        expmaskcol1->show();
        structcol->show();
        softradiuscol->show();
        showmaskcolMethod->show(); // Being able to change Color & Light mask visibility is useless in batch mode
        showmaskcolMethodinv->hide();
        gridFrame->hide();
        merge1colFrame->show();

    } else if (invers->get_active()) {
        llCurveEditorG->show();
        clCurveEditorG->hide();
        HCurveEditorG->hide();
        blurcol->hide();
        contcol->hide();
        qualitycurveMethod->hide();
        labqualcurv->hide();
        expmaskcol->show();
        expmaskcol1->hide();
        softradiuscol->hide();
        structcol->hide();
        gridFrame->hide();
        showmaskcolMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        showmaskcolMethodinv->show();
        merge1colFrame->hide();
    } else {
        llCurveEditorG->show();
        clCurveEditorG->show();
        HCurveEditorG->show();
        qualitycurveMethod->show();
        labqualcurv->show();
        blurcol->show();
        contcol->show();
        expmaskcol->show();
        expmaskcol1->show();
        structcol->show();
        gridFrame->show();
        softradiuscol->show();
        showmaskcolMethod->show(); // Being able to change Color & Light mask visibility is useless in batch mode
        showmaskcolMethodinv->hide();
        merge1colFrame->show();

        if (batchMode) {
            showmaskcolMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
            showmaskcolMethodinv->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        }
    }

    if (merMethod->get_active_row_number() == 0) {
        mask7->hide();
        conthrcol->hide();
        structcol->set_sensitive(true);
        //  strcolh->set_sensitive(false);
        sensi->set_sensitive(true);
        blurcolde->set_sensitive(true);
        blurcol->set_sensitive(true);
        contcol->set_sensitive(true);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(true);
        special->set_sensitive(true);
        invers->set_sensitive(true);
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 1) {
        mask7->hide();
        structcol->set_sensitive(true);
        //   strcolh->set_sensitive(false);
        sensi->set_sensitive(true);
        blurcolde->set_sensitive(true);
        blurcol->set_sensitive(true);
        contcol->set_sensitive(true);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(true);
        special->set_sensitive(true);
        invers->set_sensitive(true);
        conthrcol->hide();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 2) {
        mask7->show();
        structcol->set_sensitive(false);
        strcolh->set_sensitive(true);
        sensi->set_sensitive(false);
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        conthrcol->show();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 3) {
        conthrcol->show();
        structcol->set_sensitive(false);
        sensi->set_sensitive(false);
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        strcolh->set_sensitive(true);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        mask7->show();
        gridmerFrame->hide();
    } else if (merMethod->get_active_row_number() == 3) {
        mask7->show();
        structcol->set_sensitive(false);
        strcolh->set_sensitive(true);
        sensi->set_sensitive(false);
        blurcolde->set_sensitive(false);
        blurcol->set_sensitive(false);
        contcol->set_sensitive(false);
        H2CurveEditorG->set_sensitive(true);
        rgbCurveEditorG->set_sensitive(false);
        special->set_sensitive(false);
        invers->set_sensitive(false);
        conthrcol->hide();
        gridmerFrame->show();
    }

    // Update Exposure GUI according to black adjuster state (to be compliant with adjusterChanged function)
    sensiex->show();
    curveEditorG->show();
    blurexpde->show();

    if (multiImage && inversex->get_inconsistent()) {
        structexp->show();
        softradiusexp->show();
        shadex->show();
        expMethod->show();
        expmaskexp->show();
        exnoiseMethod->show();
        gradFrame->show();

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->show();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->show();
            fatFrame->show();
            softradiusexp->hide();
        }

        showmaskexpMethodinv->hide();
        showmaskexpMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
    } else if (inversex->get_active()) {
        structexp->hide();
        softradiusexp->hide();
        shadex->hide();
        expMethod->hide();
        exnoiseMethod->hide();
        pdeFrame->hide();
        fatFrame->hide();
        expmaskexp->show();
        showmaskexpMethodinv->show();
        showmaskexpMethod->hide();
        softradiusexp->show();
        gradFrame->hide();

        expMethod->set_active(0);

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->show();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->hide();
        }
    } else {
        structexp->show();
        softradiusexp->show();
        shadex->show();
        expMethod->show();
        exnoiseMethod->show();
        expmaskexp->show();
        showmaskexpMethodinv->hide();
        showmaskexpMethod->show();
        gradFrame->show();

        if (expMethod->get_active_row_number() == 0) {
            pdeFrame->hide();
            fatFrame->hide();
            softradiusexp->show();
        } else if (expMethod->get_active_row_number() == 1) {
            pdeFrame->show();
            fatFrame->show();
            softradiusexp->hide();
        }

        if (batchMode) {
            showmaskexpMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
        }
    }

    // Update SH GUI according to black adjuster state (to be compliant with adjusterChanged function)
    sensihs->show();
    blurSHde->show();

    if (multiImage && inverssh->get_inconsistent()) {
        expmasksh->show();
        showmaskSHMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
    } else if (inverssh->get_active()) {
        // printf("GUI inv SH\n");
        expmasksh->show();
        showmaskSHMethodinv->show();
        showmaskSHMethod->hide();
//        shMethod->set_active(0);
        gradSHFrame->hide();

        if (shMethod->get_active_row_number() == 0) {
            for (int i = 0; i < 5; i++) {
                multipliersh[i]->hide();
            }

            detailSH->hide();
            gamFrame->hide();
            highlights->show();
            h_tonalwidth->show();
            shadows->show();
            s_tonalwidth->show();
            sh_radius->show();
        } else if (shMethod->get_active_row_number() == 1) {
            for (int i = 0; i < 5; i++) {
                multipliersh[i]->show();
            }

            gamFrame->show();
            gradSHFrame->show();

            detailSH->show();
            highlights->hide();
            h_tonalwidth->hide();
            shadows->hide();
            s_tonalwidth->hide();
            sh_radius->hide();

        }

    } else {
        //  printf("GUI NON inv SH\n");
        expmasksh->show();
        showmaskSHMethodinv->hide();
        showmaskSHMethod->show();
        gradSHFrame->show();

        if (batchMode) {
            showmaskSHMethod->hide(); // Being able to change Color & Light mask visibility is useless in batch mode
            showmaskSHMethodinv->hide();
        }
    }

    if (multiImage && black->getEditedState() != Edited) {
        shcompr->set_sensitive(true);
    } else {
        shcompr->set_sensitive(!((int)black->getValue() == 0)); // At black = 0, shcompr value has no effect
    }

    if (multiImage && shadex->getEditedState() != Edited) {
        shcompr->set_sensitive(true);
    } else {
        shcompr->set_sensitive(!((int)shadex->getValue() == 0)); // At black = 0, shcompr value has no effect
    }

    // Update Vibrance GUI according to pastsattog button state (to be compliant with pastsattog_toggled function)
    if (multiImage && pastSatTog->get_inconsistent()) {
        psThreshold->set_sensitive(true);
        saturated->set_sensitive(true);
    } else if (pastSatTog->get_active()) {
        // Link both slider, so we set saturated and psThresholds unsensitive
        psThreshold->set_sensitive(false);
        saturated->set_sensitive(false);
        saturated->setValue(pastels->getValue()); // Pastels and Saturated are linked
    } else {
        // Separate sliders, so we set saturated and psThresholds sensitive again
        psThreshold->set_sensitive(true);
        saturated->set_sensitive(true);
    }

    if (expMethod->get_active_row_number() == 0) {
        pdeFrame->hide();
        fatFrame->hide();
        softradiusexp->show();
    } else if (expMethod->get_active_row_number() == 1) {
        pdeFrame->show();
        fatFrame->show();
        softradiusexp->hide();
    }

    if (softMethod->get_active_row_number() == 0) {
        laplace->hide();
        ctboxsoftmethod->hide();
    } else if (softMethod->get_active_row_number() == 1) {
        laplace->show();
        ctboxsoftmethod->show();
    }

    if (shMethod->get_active_row_number() == 0) {
        for (int i = 0; i < 5; i++) {
            multipliersh[i]->hide();
        }

        detailSH->hide();
        gamFrame->hide();
        highlights->show();
        h_tonalwidth->show();
        shadows->show();
        s_tonalwidth->show();
        sh_radius->show();
    } else if (shMethod->get_active_row_number() == 1) {
        for (int i = 0; i < 5; i++) {
            multipliersh[i]->show();
        }

        detailSH->show();
        gamFrame->show();
        highlights->hide();
        h_tonalwidth->hide();
        shadows->hide();
        s_tonalwidth->hide();
        sh_radius->hide();

    }

    if (blMethod->get_active_row_number() == 0) {
        radius->show();
        fftwbl->show();
        strength->show();
        itera->hide();
        isogr->show();
        grainFrame->show();
        strengr->show();
        scalegr->show();
        medMethod->hide();
        guidbl->hide();
        epsbl->hide();
        activlum->show();
    } else if (blMethod->get_active_row_number() == 1) {
        radius->hide();
        fftwbl->hide();
        strength->hide();
        grainFrame->hide();
        isogr->hide();
        strengr->hide();
        scalegr->hide();
        itera->show();
        medMethod->show();
        guidbl->hide();
        epsbl->hide();
        activlum->show();
    } else if (blMethod->get_active_row_number() == 2) {
        radius->hide();
        fftwbl->hide();
        strength->hide();
        isogr->hide();
        grainFrame->hide();
        strengr->hide();
        scalegr->hide();
        itera->hide();
        medMethod->hide();
        guidbl->show();
        epsbl->show();
        activlum->hide();
    }

// Update Retinex GUI according to inversret button state (to be compliant with inversretChanged function)
    if (multiImage && inversret->get_inconsistent()) {
        sensih->show();
        softradiusret->show();
        showmaskretiMethod->show(); // Being able to change Color & Light mask visibility is useless in batch mode
    } else if (inversret->get_active()) {
        sensih->show();
        softradiusret->show();
        showmaskretiMethod->show();
        sensih->show();
        softradiusret->show();
    }

    if (str->getValue() >= 0.2f) {
        retitoolFrame->show();
    } else {
        retitoolFrame->hide();
    }


    if (localcontMethod->get_active_row_number() == 0) {
        levelwav->hide();
        residcont->hide();
        residchro->hide();
        clarilres->hide();
        claricres->hide();
        clarisoft->hide();
        clariFrame->hide();
        csThreshold->hide();
        lcradius->show();
        lcamount->show();
        lcdarkness->show();
        lclightness->show();
        LocalcurveEditorwav->hide();
        fftwlc->show();
    } else if (localcontMethod->get_active_row_number() == 1) {
        levelwav->show();
        residcont->show();
        residchro->show();
        clarilres->show();
        claricres->show();
        clarisoft->show();
        clariFrame->show();
        csThreshold->show();
        lcradius->hide();
        lcamount->hide();
        lcdarkness->hide();
        lclightness->hide();
        LocalcurveEditorwav->show();
        fftwlc->hide();
    }

// Update Sharpening GUI according to inverssha button state (to be compliant with inversshaChanged function)
    sensisha->show();
}

void Locallab::autoOpenCurve()
{
    // printf("autoOpenCurve\n");

    // TODO autoOpenCurve only considers linearity state of selected spot curve
//    llshape->openIfNonlinear();
//    ccshape->openIfNonlinear();
//    LHshape->openIfNonlinear();
//    HHshape->openIfNonlinear();
//    CCmaskshape->openIfNonlinear();
//    LLmaskshape->openIfNonlinear();
//    HHmaskshape->openIfNonlinear();
//    CCmaskexpshape->openIfNonlinear();
//    LLmaskexpshape->openIfNonlinear();
}
