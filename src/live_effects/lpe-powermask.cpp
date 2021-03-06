/*
 * Released under GNU GPL, read the file 'COPYING' for more information
 */
#include "live_effects/lpe-powermask.h"
#include <2geom/path-intersection.h>
#include <2geom/intersection-graph.h>
#include "display/curve.h"
#include "helper/geom.h"
#include "sp-mask.h"
#include "sp-path.h"
#include "sp-shape.h"
#include "sp-defs.h"
#include "style.h"
#include "sp-item-group.h"
#include "svg/svg.h"
#include "ui/tools-switch.h"
#include "path-chemistry.h"
#include "uri.h"
#include "extract-uri.h"
#include <bad-uri-exception.h>

// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape {
namespace LivePathEffect {

LPEPowerMask::LPEPowerMask(LivePathEffectObject *lpeobject)
    : Effect(lpeobject),
    uri("Store the uri of mask", "", "uri", &wr, this, "false", false),
    invert(_("Invert mask"), _("Invert mask"), "invert", &wr, this, false),
    wrap(_("Wrap mask data"), _("Wrap mask data allowing previous filters"), "wrap", &wr, this, false),
    hide_mask(_("Hide mask"), _("Hide mask"), "hide_mask", &wr, this, false),
    background(_("Add background to mask"), _("Add background to mask"), "background", &wr, this, false),
    background_style(_("Background Style"), _("CSS to background"), "background_style", &wr, this,"fill:#ffffff;opacity:1;")
{
    registerParameter(&uri);
    registerParameter(&invert);
    registerParameter(&wrap);
    registerParameter(&hide_mask);
    registerParameter(&background);
    registerParameter(&background_style);
    //lock.param_setValue(false);
    background_style.param_hide_canvas_text();
}

LPEPowerMask::~LPEPowerMask() {}

void
LPEPowerMask::doBeforeEffect (SPLPEItem const* lpeitem){
    SPObject * mask = SP_ITEM(sp_lpe_item)->mask_ref->getObject();
    if(hide_mask && mask) {
        SP_ITEM(sp_lpe_item)->mask_ref->detach();
    } else if (!hide_mask && !mask && uri.param_getSVGValue()) {
        try {
            SP_ITEM(sp_lpe_item)->mask_ref->attach(Inkscape::URI(uri.param_getSVGValue()));
        } catch (Inkscape::BadURIException &e) {
            g_warning("%s", e.what());
            SP_ITEM(sp_lpe_item)->mask_ref->detach();
        }
    }
    mask = SP_ITEM(sp_lpe_item)->mask_ref->getObject();
    if (mask) {
        uri.param_setValue(Glib::ustring(extract_uri(sp_lpe_item->getRepr()->attribute("mask"))), true);
        SP_ITEM(sp_lpe_item)->mask_ref->detach();
        Geom::OptRect bbox = sp_lpe_item->visualBounds();
        if(!bbox) {
            return;
        }
        if (uri.param_getSVGValue()) {
            try {
                SP_ITEM(sp_lpe_item)->mask_ref->attach(Inkscape::URI(uri.param_getSVGValue()));
            } catch (Inkscape::BadURIException &e) {
                g_warning("%s", e.what());
                SP_ITEM(sp_lpe_item)->mask_ref->detach();
            }
        } else {
            SP_ITEM(sp_lpe_item)->mask_ref->detach();
        }
        Geom::Rect bboxrect = (*bbox);
        bboxrect.expandBy(1);
        Geom::Point topleft      = bboxrect.corner(0);
        Geom::Point topright     = bboxrect.corner(1);
        Geom::Point bottomright  = bboxrect.corner(2);
        Geom::Point bottomleft   = bboxrect.corner(3);
        mask_box.clear();
        mask_box.start(topleft);
        mask_box.appendNew<Geom::LineSegment>(topright);
        mask_box.appendNew<Geom::LineSegment>(bottomright);
        mask_box.appendNew<Geom::LineSegment>(bottomleft);
        mask_box.close();
        setMask();
    }
}

void
LPEPowerMask::setMask(){
    SPMask *mask = SP_ITEM(sp_lpe_item)->mask_ref->getObject();
    SPObject *elemref = NULL;
    SPDocument * document = SP_ACTIVE_DOCUMENT;
    if (!document || !mask) {
        return;
    }
    Inkscape::XML::Node *root = sp_lpe_item->document->getReprRoot();
    Inkscape::XML::Node *root_origin = document->getReprRoot();
    if (root_origin != root) {
        return;
    }
    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *box = NULL;
    Inkscape::XML::Node *filter = NULL;
    SPDefs * defs = document->getDefs();
    Glib::ustring mask_id = (Glib::ustring)mask->getId();
    Glib::ustring box_id = mask_id + (Glib::ustring)"_box";
    Glib::ustring filter_id = mask_id + (Glib::ustring)"_inverse";
    Glib::ustring filter_label = (Glib::ustring)"filter" + mask_id;
    Glib::ustring filter_uri = (Glib::ustring)"url(#" + filter_id + (Glib::ustring)")";
    if (!(elemref = document->getObjectById(filter_id))) {
        filter = xml_doc->createElement("svg:filter");
        filter->setAttribute("id", filter_id.c_str());
        filter->setAttribute("inkscape:label", filter_label.c_str());
        SPCSSAttr *css = sp_repr_css_attr_new();
        sp_repr_css_set_property(css, "color-interpolation-filters", "sRGB");
        sp_repr_css_change(filter, css, "style");
        sp_repr_css_attr_unref(css);
        filter->setAttribute("height", "100");
        filter->setAttribute("width", "100");
        filter->setAttribute("x", "-50");
        filter->setAttribute("y", "-50");
        Inkscape::XML::Node *primitive1 =  xml_doc->createElement("svg:feColorMatrix");
        Glib::ustring primitive1_id = (mask_id + (Glib::ustring)"_primitive1").c_str();
        primitive1->setAttribute("id", primitive1_id.c_str());
        primitive1->setAttribute("values", "1");
        primitive1->setAttribute("type", "saturate");
        primitive1->setAttribute("result", "fbSourceGraphic");
        Inkscape::XML::Node *primitive2 =  xml_doc->createElement("svg:feColorMatrix");
        Glib::ustring primitive2_id = (mask_id + (Glib::ustring)"_primitive2").c_str();
        primitive2->setAttribute("id", primitive2_id.c_str());
        primitive2->setAttribute("values", "-1 0 0 0 1 0 -1 0 0 1 0 0 -1 0 1 0 0 0 1 0 ");
        primitive2->setAttribute("in", "fbSourceGraphic");
        elemref = defs->appendChildRepr(filter);
        Inkscape::GC::release(filter);
        filter->appendChild(primitive1);
        Inkscape::GC::release(primitive1);
        filter->appendChild(primitive2);
        Inkscape::GC::release(primitive2);
    }
    if(wrap && is_visible){
        Glib::ustring g_data_id = mask_id + (Glib::ustring)"_container";
        if((elemref = document->getObjectById(g_data_id))){
            elemref->getRepr()->setPosition(-1);
        } else {
            Inkscape::XML::Node * container = xml_doc->createElement("svg:g");
            container->setAttribute("id", g_data_id.c_str());
            mask->appendChildRepr(container);
            std::vector<SPObject*> mask_list = mask->childList(true);
            container->setPosition(-1);
            Inkscape::GC::release(container);
            for ( std::vector<SPObject*>::const_iterator iter=mask_list.begin();iter!=mask_list.end();++iter) {
                SPItem * mask_data = SP_ITEM(*iter);
                Inkscape::XML::Node *mask_node = mask_data->getRepr();
                if (! strcmp(mask_data->getId(), box_id.c_str()) ||
                    ! strcmp(mask_data->getId(), g_data_id.c_str()))
                {
                    continue;
                }
                SPCSSAttr *css = sp_repr_css_attr_new();
                if(mask_node->attribute("style")) {
                    sp_repr_css_attr_add_from_string(css, mask_node->attribute("style"));
                }
                char const* filter = sp_repr_css_property (css, "filter", NULL);
                if(!filter || !strcmp(filter, filter_uri.c_str())) {
                    sp_repr_css_set_property (css, "filter", NULL);
                }
                Glib::ustring css_str;
                sp_repr_css_write_string(css, css_str);
                mask_node->setAttribute("style", css_str.c_str());
                mask->getRepr()->removeChild(mask_node);
                container->appendChild(mask_node);
                Inkscape::GC::release(mask_node);
            }
        }
    } else {
        Glib::ustring g_data_id = mask_id + (Glib::ustring)"_container";
        if((elemref = document->getObjectById(g_data_id))){
            std::vector<SPItem*> item_list = sp_item_group_item_list(SP_GROUP(elemref));
            for ( std::vector<SPItem*>::const_iterator iter=item_list.begin();iter!=item_list.end();++iter) {
                Inkscape::XML::Node *mask_node = (*iter)->getRepr();
                elemref->getRepr()->removeChild(mask_node);
                mask->getRepr()->appendChild(mask_node);
                Inkscape::GC::release(mask_node);
            }
            sp_object_ref(elemref, 0 );
            elemref->deleteObject(true);
            sp_object_unref(elemref);
        }
    }
    std::vector<SPObject*> mask_list = mask->childList(true);
    for ( std::vector<SPObject*>::const_iterator iter=mask_list.begin();iter!=mask_list.end();++iter) {
        SPItem * mask_data = SP_ITEM(*iter);
        Inkscape::XML::Node *mask_node = mask_data->getRepr();
        if (! strcmp(mask_data->getId(), box_id.c_str())){
            continue;
        }
        Glib::ustring mask_data_id = (Glib::ustring)mask_data->getId();
        SPCSSAttr *css = sp_repr_css_attr_new();
        if(mask_node->attribute("style")) {
            sp_repr_css_attr_add_from_string(css, mask_node->attribute("style"));
        }
        char const* filter = sp_repr_css_property (css, "filter", NULL);
        if(!filter || !strcmp(filter, filter_uri.c_str())) {
            if (invert && is_visible) {
                sp_repr_css_set_property (css, "filter", filter_uri.c_str());
            } else {
                sp_repr_css_set_property (css, "filter", NULL);
            }
            Glib::ustring css_str;
            sp_repr_css_write_string(css, css_str);
            mask_node->setAttribute("style", css_str.c_str());
        }
    }
    if ((elemref = document->getObjectById(box_id))) {
        elemref->deleteObject(true);
    }
    if (background && is_visible) {
        bool exist = true;
        if (!(elemref = document->getObjectById(box_id))) {
            box = xml_doc->createElement("svg:path");
            box->setAttribute("id", box_id.c_str());
            exist = false;
        }
        box->setAttribute("style", background_style.param_getSVGValue());
        gchar * box_str = sp_svg_write_path( mask_box );
        box->setAttribute("d" , box_str);
        g_free(box_str);
        if (!exist) {
            elemref = mask->appendChildRepr(box);
            Inkscape::GC::release(box);
        }
        box->setPosition(1);
    } else if ((elemref = document->getObjectById(box_id))) {
        elemref->deleteObject(true);
    }
    mask->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void 
LPEPowerMask::doOnVisibilityToggled(SPLPEItem const* lpeitem)
{
    doBeforeEffect(lpeitem);
}

void
LPEPowerMask::doEffect (SPCurve * curve)
{
}

//void
//LPEPowerMask::transform_multiply(Geom::Affine const& postmul, bool set)
//{
//    SPMask *mask_path = SP_ITEM(sp_lpe_item)->mask_ref->getObject();
//    if (mask_path && lock) {
//        SPMask *mask_path = SP_ITEM(sp_lpe_item)->mask_ref->getObject();
//        std::vector<SPObject*> mask_path_list = mask_path->childList(true);
//        Glib::ustring mask_id = (Glib::ustring)mask_path->getId();
//        Glib::ustring box_id = mask_id + (Glib::ustring)"_box";
//        for ( std::vector<SPObject*>::const_iterator iter=mask_path_list.begin();iter!=mask_path_list.end();++iter) {
//            SPObject * mask_data = *iter;
//            if (! strcmp(mask_data->getId(), box_id.c_str())){
//                continue;
//            }
//            SP_ITEM(mask_data)->transform *= postmul.inverse();
//        }
//    }
//    //cycle through all parameters. Most parameters will not need transformation, but path and point params
//    for (std::vector<Parameter *>::iterator it = param_vector.begin(); it != param_vector.end(); ++it) {
//        Parameter * param = *it;
//        param->param_transform_multiply(postmul, set);
//    }
//    sp_lpe_item_update_patheffect(SP_LPE_ITEM(sp_lpe_item), false, false);
//}



void 
LPEPowerMask::doOnRemove (SPLPEItem const* lpeitem)
{
    if(!keep_paths) {
        SPMask *mask = lpeitem->mask_ref->getObject();
        if (mask) {
            invert.param_setValue(false);
            wrap.param_setValue(false);
            background.param_setValue(false);
            setMask();
            SPObject *elemref = NULL;
            SPDocument * document = SP_ACTIVE_DOCUMENT;
            Glib::ustring mask_id = (Glib::ustring)mask->getId();
            Glib::ustring filter_id = mask_id + (Glib::ustring)"_inverse";
            if ((elemref = document->getObjectById(filter_id))) {
                elemref->deleteObject(true);
            }
        }
    }
}

}; //namespace LivePathEffect
}; /* namespace Inkscape */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
