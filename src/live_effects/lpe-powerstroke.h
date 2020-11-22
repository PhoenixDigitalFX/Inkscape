// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief PowerStroke LPE effect, see lpe-powerstroke.cpp.
 */
/* Authors:
 *   Johan Engelen <j.b.c.engelen@alumnus.utwente.nl>
 *
 * Copyright (C) 2010-2011 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_LPE_POWERSTROKE_H
#define INKSCAPE_LPE_POWERSTROKE_H

#include "live_effects/effect.h"
#include "live_effects/parameter/bool.h"
#include "live_effects/parameter/enum.h"
#include "live_effects/parameter/hidden.h"
#include "live_effects/parameter/powerstrokepointarray.h"

namespace Inkscape {
namespace LivePathEffect {

enum LineCapType { LINECAP_BUTT, LINECAP_SQUARE, LINECAP_ROUND, LINECAP_PEAK, LINECAP_ZERO_WIDTH };

static const Util::EnumData<unsigned> LineCapTypeData[] = { { LINECAP_BUTT, N_("Butt"), "butt" },
                                                            { LINECAP_SQUARE, N_("Square"), "square" },
                                                            { LINECAP_ROUND, N_("Round"), "round" },
                                                            { LINECAP_PEAK, N_("Peak"), "peak" },
                                                            { LINECAP_ZERO_WIDTH, N_("Zero width"), "zerowidth" } };
static const Util::EnumDataConverter<unsigned> LineCapTypeConverter(LineCapTypeData,
                                                                    sizeof(LineCapTypeData) / sizeof(*LineCapTypeData));

class LPEPowerStroke : public Effect {
public:
    LPEPowerStroke(LivePathEffectObject *lpeobject);
    ~LPEPowerStroke() override;
    LPEPowerStroke(const LPEPowerStroke&) = delete;
    LPEPowerStroke& operator=(const LPEPowerStroke&) = delete;
    
    Geom::PathVector doEffect_path (Geom::PathVector const & path_in) override;
    void doBeforeEffect(SPLPEItem const *lpeItem) override;
    void doOnApply(SPLPEItem const* lpeitem) override;
    void doOnRemove(SPLPEItem const* lpeitem) override;
    void doOnLoad (SPLPEItem const* lpeitem) override;
    void doOnFork (SPLPEItem const* lpeitem) override;
    void doAfterEffect(SPLPEItem const *lpeitem, SPCurve *curve) override;
    void doOnVisibilityToggled(SPLPEItem const* /*lpeitem*/) override;
    void transform_multiply(Geom::Affine const &postmul, bool set) override;
    void applyStyle(SPLPEItem *lpeitem);
    void createStroke(Geom::PathVector stroke);
    void inicialize();
    void doAfterAllEffects (SPLPEItem const* /*lpeitem*/) override;
    void modified(SPObject *obj, guint flags);
    void satellite_modified(SPObject *obj, guint flags);
    void parent_modified(SPObject *obj, guint flags);
    void regenerateItems();
    void upgradeLegacy();
    // methods called by path-manipulator upon edits
    void adjustForNewPath(Geom::PathVector const & path_in);

    PowerStrokePointArrayParam offset_points;
    BoolParam not_jump;
private:
    static int _enableforked(gpointer data);
    BoolParam sort_points;
    EnumParam<unsigned> interpolator_type;
    ScalarParam interpolator_beta;
    ScalarParam scale_width;
    EnumParam<unsigned> start_linecap_type;
    EnumParam<unsigned> linejoin_type;
    ScalarParam miter_limit;
    EnumParam<unsigned> end_linecap_type;
    SPObject * elemref;
    size_t recusion_limit;
    sigc::connection modified_connection;
    sigc::connection satellite_modified_connection;
    sigc::connection parent_modified_connection;
    bool has_recursion;
    bool transforming;
    bool forked;
    size_t displays;
};

} //namespace LivePathEffect
} //namespace Inkscape

#endif

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
