/*!The Graphic Box Library
 * 
 * GBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * GBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with GBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2014 - 2015, ruki All rights reserved.
 *
 * @author      ruki
 * @file        path.c
 * @ingroup     core
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_MODULE_NAME            "path"
#define TB_TRACE_MODULE_DEBUG           (0)

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "path.h"
#include "impl/bounds.h"
#include "impl/geometry.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

// the points grow count
#ifdef __gb_small__
#   define GB_PATH_POINTS_GROW      (32)
#else
#   define GB_PATH_POINTS_GROW      (64)
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the path flag enum
typedef enum __gb_path_flag_e
{
    GB_PATH_FLAG_DIRTY_HINT             = 1
,   GB_PATH_FLAG_DIRTY_BOUNDS           = 2
,   GB_PATH_FLAG_DIRTY_POLYGON          = 4
,   GB_PATH_FLAG_DIRTY_ALL              = GB_PATH_FLAG_DIRTY_HINT | GB_PATH_FLAG_DIRTY_BOUNDS | GB_PATH_FLAG_DIRTY_POLYGON
,   GB_PATH_FLAG_HAVE_CURVE             = 8
,   GB_PATH_FLAG_CONVEX_POLYGON         = 16
,   GB_PATH_FLAG_CLOSED                 = 32

}gb_path_flag_e;

// the path impl type
typedef struct __gb_path_impl_t
{
    // the iterator 
    tb_iterator_t       itor;

    // the flag
    tb_uint8_t          flag;

    // the hint shape
    gb_shape_t          hint;

    // the polygon
    gb_polygon_t        polygon;

    // the bounds
    gb_rect_t           bounds;

    // the head for the current contour
    gb_point_t          head;

    // the itor item
    gb_path_item_t      item;

    // the codes, tb_uint8_t[]
    tb_vector_ref_t     codes;

    // the points, gb_point_t[]
    tb_vector_ref_t     points;

    // the polygon points, gb_point_t[]
    tb_vector_ref_t     polygon_points;

    // the polygon counts, gb_uint16_t[]
    tb_vector_ref_t     polygon_counts;

}gb_path_impl_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
static tb_size_t gb_path_itor_size(tb_iterator_ref_t iterator)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)iterator;
    tb_assert_return_val(impl && impl->codes, 0);

    // size
    return tb_vector_size(impl->codes);
}
static tb_size_t gb_path_itor_head(tb_iterator_ref_t iterator)
{
    // head
    return 0;
}
static tb_size_t gb_path_itor_last(tb_iterator_ref_t iterator)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)iterator;
    tb_assert_return_val(impl, 0);

    // the point count for code
    static tb_uint16_t s_point_count_for_code[] = {1, 1, 2, 3, 0};

    // the last code index
    tb_size_t code_last = tb_vector_size(impl->codes);
    if (code_last) code_last--;
    
    // the last code
    tb_size_t code = (tb_size_t)tb_iterator_item(impl->codes, code_last);
    tb_assert_abort(code < tb_arrayn(s_point_count_for_code));

    // the last point step
    tb_size_t point_step = s_point_count_for_code[code];

    // the last point index
    tb_size_t point_last = tb_vector_size(impl->points);
    if (point_last >= point_step) point_last -= point_step;

    // last
    return ((code_last << 16) | point_last);
}
static tb_size_t gb_path_itor_tail(tb_iterator_ref_t iterator)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)iterator;
    tb_assert_return_val(impl && impl->codes && impl->points, 0);

    // the code and point tail
    tb_size_t code_tail     = tb_vector_size(impl->codes);
    tb_size_t point_tail    = tb_vector_size(impl->points);
    tb_assert_abort(code_tail <= TB_MAXU16 && point_tail <= TB_MAXU16);

    // tail
    return ((code_tail << 16) | point_tail);
}
static tb_size_t gb_path_itor_next(tb_iterator_ref_t iterator, tb_size_t itor)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)iterator;
    tb_assert_return_val(impl && impl->codes, 0);

    // the point count for code
    static tb_uint16_t s_point_count_for_code[] = {1, 1, 2, 3, 0};

    // the code and point index
    tb_size_t code_index    = itor >> 16;
    tb_size_t point_index   = itor & 0xffff;

    // the code
    tb_size_t code = (tb_size_t)tb_iterator_item(impl->codes, code_index);
    tb_assert_abort(code < tb_arrayn(s_point_count_for_code));

    // next code index
    code_index++;

    // next point index
    point_index += s_point_count_for_code[code];

    // next
    return ((code_index << 16) | point_index);
}
static tb_pointer_t gb_path_itor_item(tb_iterator_ref_t iterator, tb_size_t itor)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)iterator;
    tb_assert_return_val(impl && impl->codes && impl->points, tb_null);
    
    // the code and point index
    tb_size_t code_index    = itor >> 16;
    tb_size_t point_index   = itor & 0xffff;

    // init item
    impl->item.code = (tb_size_t)tb_iterator_item(impl->codes, code_index);
    switch (impl->item.code)
    {
    case GB_PATH_CODE_MOVE:
        {
            // the point
            gb_point_ref_t point = (gb_point_ref_t)tb_iterator_item(impl->points, point_index);
            tb_assert_abort(point);

            // save point
            impl->item.point = *point;
        }
        break;
    case GB_PATH_CODE_LINE:
        {
            // the point
            gb_point_ref_t point = (gb_point_ref_t)tb_iterator_item(impl->points, point_index);
            tb_assert_abort(point);

            // save point
            impl->item.point = *point;
        }
        break;
    case GB_PATH_CODE_QUAD:
        {
            // the ctrl
            gb_point_ref_t ctrl = (gb_point_ref_t)tb_iterator_item(impl->points, point_index);
            tb_assert_abort(ctrl);

            // the point
            gb_point_ref_t point = (gb_point_ref_t)tb_iterator_item(impl->points, point_index + 1);
            tb_assert_abort(point);

            // save points
            impl->item.ctrls[0] = *ctrl;
            impl->item.point    = *point;
        }
        break;
    case GB_PATH_CODE_CUBE:
        {
            // the ctrl0
            gb_point_ref_t ctrl0 = (gb_point_ref_t)tb_iterator_item(impl->points, point_index);
            tb_assert_abort(ctrl0);

            // the ctrl1
            gb_point_ref_t ctrl1 = (gb_point_ref_t)tb_iterator_item(impl->points, point_index + 1);
            tb_assert_abort(ctrl1);

            // the point
            gb_point_ref_t point = (gb_point_ref_t)tb_iterator_item(impl->points, point_index + 2);
            tb_assert_abort(point);

            // save points
            impl->item.ctrls[0] = *ctrl0;
            impl->item.ctrls[1] = *ctrl1;
            impl->item.point    = *point;
        }
        break;
    case GB_PATH_CODE_CLOS:
        break;
    default:
        // trace
        tb_trace_e("invalid code: %lu", impl->item.code);
        return tb_null;
    }

    // data
    return &impl->item;
}
static tb_bool_t gb_path_python_is_convex(gb_path_impl_t* impl)
{
    // check
    tb_assert_and_check_return_val(impl, tb_false);

    // analyze convex from the hint shape first
    if (!(impl->flag & GB_PATH_FLAG_DIRTY_HINT) && impl->hint.type)
    {
        // done
        switch (impl->hint.type)
        {
        case GB_SHAPE_TYPE_RECT:
        case GB_SHAPE_TYPE_CIRCLE:
        case GB_SHAPE_TYPE_ELLIPSE:
        case GB_SHAPE_TYPE_TRIANGLE:
            return tb_true;
        }

        // not convex
        return tb_false;
    }

    // TODO
    return tb_false;
}
static tb_bool_t gb_path_make_hint(gb_path_impl_t* impl)
{ 
    // check
    tb_assert_and_check_return_val(impl && impl->codes && impl->points, tb_false);

    // clear hint first
    impl->hint.type = GB_SHAPE_TYPE_NONE;

    // no curve? make bounds
    if (!(impl->flag & GB_PATH_FLAG_HAVE_CURVE))
    {
        // the codes 
        tb_uint8_t const* codes = (tb_uint8_t const*)tb_vector_data(impl->codes);
        tb_assert_and_check_return_val(codes, tb_false);

        // the points 
        gb_point_ref_t points = (gb_point_ref_t)tb_vector_data(impl->points);
        tb_assert_and_check_return_val(points, tb_false);

        // the points count
        tb_size_t count = tb_vector_size(impl->points);

        // rect?
        if (    count == 5
            &&  points[0].x == points[4].x
            &&  points[0].y == points[4].y
            &&  codes[0] == GB_PATH_CODE_MOVE
            &&  codes[1] == GB_PATH_CODE_LINE
            &&  codes[2] == GB_PATH_CODE_LINE
            &&  codes[3] == GB_PATH_CODE_LINE
            &&  codes[4] == GB_PATH_CODE_LINE
            &&  (   (   points[0].x != points[1].x && points[0].y == points[1].y
                    &&  points[1].x == points[2].x && points[1].y != points[2].y
                    &&  points[2].x != points[3].x && points[2].y == points[3].y
                    &&  points[3].x == points[4].x && points[3].y != points[4].y)
                ||
                    (   points[0].x == points[1].x && points[0].y != points[1].y
                    &&  points[1].x != points[2].x && points[1].y == points[2].y
                    &&  points[2].x == points[3].x && points[2].y != points[3].y
                    &&  points[3].x != points[4].x && points[3].y == points[4].y)))
        {
            // make hint
            impl->hint.type = GB_SHAPE_TYPE_RECT;
            gb_bounds_make(&impl->hint.u.rect, points, 4);

            // trace
            tb_trace_d("make: hint: %{rect}", &impl->hint.u.rect);
        }
        // line?
        else if (   count == 2
                &&  codes[0] == GB_PATH_CODE_MOVE
                &&  codes[1] == GB_PATH_CODE_LINE
                &&  points[0].x != points[1].x
                &&  points[0].y != points[1].y)
        {
            // make hint
            impl->hint.type         = GB_SHAPE_TYPE_LINE;
            impl->hint.u.line.p0    = points[0];
            impl->hint.u.line.p1    = points[1];

            // trace
            tb_trace_d("make: hint: %{line}", &impl->hint.u.line);
        }
        // point
        else if (count == 1 && codes[0] == GB_PATH_CODE_MOVE)
        {
            // make hint
            impl->hint.type     = GB_SHAPE_TYPE_POINT;
            impl->hint.u.point  = points[0];

            // trace
            tb_trace_d("make: hint: %{point}", &impl->hint.u.point);
        }
    }
    
    // ok
    return tb_true;
}
static tb_void_t gb_path_make_arc_to(gb_point_ref_t ctrl, gb_point_ref_t point, tb_cpointer_t priv)
{
    // check
    tb_assert_abort(priv && point);

    // append point
    ctrl? gb_path_quad_to((gb_path_ref_t)priv, ctrl, point) : gb_path_line_to((gb_path_ref_t)priv, point);
}
static tb_void_t gb_path_make_add_arc(gb_point_ref_t ctrl, gb_point_ref_t point, tb_cpointer_t priv)
{
    // check
    tb_assert_abort(priv && point);

    // append point
    ctrl? gb_path_quad_to((gb_path_ref_t)priv, ctrl, point) : gb_path_move_to((gb_path_ref_t)priv, point);
}
static tb_void_t gb_path_make_quad_or_cube_to(gb_point_ref_t point, tb_cpointer_t priv)
{
    // check
    tb_value_t* values = (tb_value_t*)priv;
    tb_assert_abort(values && point);

    // the polygon points
    tb_vector_ref_t polygon_points = (tb_vector_ref_t)values[0].ptr;
    tb_assert_abort(polygon_points);

    // append point
    tb_vector_insert_tail(polygon_points, point);

    // update the points count
    values[1].u16++;
}
static tb_bool_t gb_path_make_python(gb_path_impl_t* impl)
{ 
    // check
    tb_assert_and_check_return_val(impl && impl->codes && impl->points, tb_false);

    // make polygon counts
    if (!impl->polygon_counts) impl->polygon_counts = tb_vector_init(8, tb_item_func_uint16());
    tb_assert_and_check_return_val(impl->polygon_counts, tb_false);

    // have curve?
    if (impl->flag & GB_PATH_FLAG_HAVE_CURVE)
    {
        // make polygon points
        if (!impl->polygon_points) impl->polygon_points = tb_vector_init(tb_vector_size(impl->points), tb_item_func_mem(sizeof(gb_point_t), tb_null, tb_null));
        tb_assert_and_check_return_val(impl->polygon_points, tb_false);

        // the points
        gb_point_ref_t points = (gb_point_ref_t)tb_vector_data(impl->points);
        tb_assert_and_check_return_val(points, tb_false);

        // clear polygon points and counts
        tb_vector_clear(impl->polygon_points);
        tb_vector_clear(impl->polygon_counts);

        // init values
        tb_value_t values[2];
        values[0].ptr = impl->polygon_points;
        values[1].u16 = 0;

        // init polygon points and counts
        gb_point_ref_t last = tb_null;
        tb_for_all (tb_uint8_t, code, impl->codes)
        {
            // done
            switch (code)
            {
            case GB_PATH_CODE_MOVE:
            case GB_PATH_CODE_LINE:
                {
                    // append count
                    if (code == GB_PATH_CODE_MOVE) 
                    {
                        if (values[1].u16) tb_vector_insert_tail(impl->polygon_counts, tb_u2p(values[1].u16));
                        values[1].u16 = 0;
                    }

                    // make point
                    gb_point_ref_t point = points++;
                    tb_vector_insert_tail(impl->polygon_points, point);

                    // save last point
                    last = point;

                    // update the points count
                    values[1].u16++;
                }
                break;
            case GB_PATH_CODE_QUAD:
                {
                    // make quad points
                    gb_point_ref_t ctrl = points++;
                    gb_point_ref_t point = points++;
                    gb_geometry_make_quad(last, ctrl, point, gb_path_make_quad_or_cube_to, values);

                    // save last point
                    last = point;
                }
                break;
            case GB_PATH_CODE_CUBE:
                {
                    // make cube points
                    gb_point_ref_t ctrl0 = points++;
                    gb_point_ref_t ctrl1 = points++;
                    gb_point_ref_t point = points++;
                    gb_geometry_make_cube(last, ctrl0, ctrl1, point, gb_path_make_quad_or_cube_to, values);

                    // save last point
                    last = point;
                }
                break;
            case GB_PATH_CODE_CLOS:
            default:
                break;
            }
        }

        // append the last count
        if (values[1].u16)
        {
            tb_vector_insert_tail(impl->polygon_counts, tb_u2p(values[1].u16));
            values[1].u16 = 0;
        }

        // append the tail count
        tb_vector_insert_tail(impl->polygon_counts, (tb_cpointer_t)0);

        // init polygon
        impl->polygon.points = (gb_point_ref_t)tb_vector_data(impl->polygon_points);
        impl->polygon.counts = (tb_uint16_t*)tb_vector_data(impl->polygon_counts);
    }
    // only move-to and line-to? using the points directly
    else
    {
        // the point count for code
        static tb_uint16_t s_point_count_for_code[] = {1, 1, 2, 3, 0};

        // init polygon counts
        tb_uint16_t count = 0;
        tb_vector_clear(impl->polygon_counts);
        tb_for_all (tb_uint8_t, code, impl->codes)
        {
            // check
            tb_assert_abort(code < tb_arrayn(s_point_count_for_code));

            // append count
            if (code == GB_PATH_CODE_MOVE) 
            {
                if (count) tb_vector_insert_tail(impl->polygon_counts, tb_u2p(count));
                count = 0;
            }

            // update count
            count += s_point_count_for_code[code];
        }

        // append the last count
        if (count)
        {
            tb_vector_insert_tail(impl->polygon_counts, tb_u2p(count));
            count = 0;
        }

        // append the tail count
        tb_vector_insert_tail(impl->polygon_counts, (tb_cpointer_t)0);

        // init polygon
        impl->polygon.points = (gb_point_ref_t)tb_vector_data(impl->points);
        impl->polygon.counts = (tb_uint16_t*)tb_vector_data(impl->polygon_counts);
    }

    // check
    tb_assert_and_check_return_val(impl->polygon.points && impl->polygon.counts, tb_false);

    // is convex polygon?
    impl->polygon.convex = (impl->flag & GB_PATH_FLAG_CONVEX_POLYGON)? tb_true : gb_path_python_is_convex(impl);

    // ok
    return tb_true;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
gb_path_ref_t gb_path_init()
{
    // done
    tb_bool_t           ok = tb_false;
    gb_path_impl_t*     impl = tb_null;
    do
    {
        // make path
        impl = tb_malloc0_type(gb_path_impl_t);
        tb_assert_and_check_break(impl);

        // init hint
        impl->hint.type = GB_SHAPE_TYPE_NONE;

        // init flag
        impl->flag = GB_PATH_FLAG_DIRTY_ALL | GB_PATH_FLAG_CLOSED;

        // init codes
        impl->codes = tb_vector_init(GB_PATH_POINTS_GROW >> 1, tb_item_func_uint8());
        tb_assert_and_check_break(impl->codes);

        // init points
        impl->points = tb_vector_init(GB_PATH_POINTS_GROW, tb_item_func_mem(sizeof(gb_point_t), tb_null, tb_null));
        tb_assert_and_check_break(impl->points);

        // init iterator
        impl->itor.mode = TB_ITERATOR_MODE_FORWARD | TB_ITERATOR_MODE_READONLY;
        impl->itor.priv = tb_null;
        impl->itor.step = sizeof(gb_path_item_t);
        impl->itor.size = gb_path_itor_size;
        impl->itor.head = gb_path_itor_head;
        impl->itor.last = gb_path_itor_last;
        impl->itor.tail = gb_path_itor_tail;
        impl->itor.next = gb_path_itor_next;
        impl->itor.item = gb_path_itor_item;

        // ok
        ok = tb_true;

    } while (0);

    // failed?
    if (!ok)
    {
        // exit it
        if (impl) gb_path_exit((gb_path_ref_t)impl);
        impl = tb_null;
    }

    // ok?
    return (gb_path_ref_t)impl;
}
tb_void_t gb_path_exit(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl);

    // exit polygon points
    if (impl->polygon_points) tb_vector_exit(impl->polygon_points);
    impl->polygon_points = tb_null;

    // exit polygon counts
    if (impl->polygon_counts) tb_vector_exit(impl->polygon_counts);
    impl->polygon_counts = tb_null;

    // exit points
    if (impl->points) tb_vector_exit(impl->points);
    impl->points = tb_null;

    // exit codes
    if (impl->codes) tb_vector_exit(impl->codes);
    impl->codes = tb_null;

    // exit it
    tb_free(impl);
}
tb_void_t gb_path_clear(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points);

    // mark dirty
    impl->flag = GB_PATH_FLAG_DIRTY_ALL;

    // clear codes
    tb_vector_clear(impl->codes);

    // clear points
    tb_vector_clear(impl->points);
}
tb_void_t gb_path_copy(gb_path_ref_t path, gb_path_ref_t copied)
{
    // check
    gb_path_impl_t* impl        = (gb_path_impl_t*)path;
    gb_path_impl_t* impl_copied = (gb_path_impl_t*)copied;
    tb_assert_and_check_return(impl && impl->codes && impl->points);
    tb_assert_and_check_return(impl_copied && impl_copied->codes && impl_copied->points);

    // null? clear it
    if (gb_path_null(copied)) 
    {
        gb_path_clear(path);
        return ;
    }

    // copy codes
    tb_vector_copy(impl->codes, impl_copied->codes);

    // copy points
    tb_vector_copy(impl->points, impl_copied->points);

    // copy flag
    impl->flag = impl_copied->flag | GB_PATH_FLAG_DIRTY_POLYGON;

    // copy hint
    impl->hint = impl_copied->hint;

    // copy head
    impl->head = impl_copied->head;

    // copy bounds
    impl->bounds = impl_copied->bounds;
}
tb_bool_t gb_path_null(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return_val(impl && impl->codes, tb_true);

    // null?
    return tb_vector_size(impl->codes)? tb_false : tb_true;
}
gb_rect_ref_t gb_path_bounds(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return_val(impl && impl->points, tb_null);

    // null?
    if (gb_path_null(path)) return tb_null;

    // dirty? make bounds
    if (impl->flag & GB_PATH_FLAG_DIRTY_BOUNDS)
    {
        // attempt to make bounds from the hint shape first
        if (!(impl->flag & GB_PATH_FLAG_DIRTY_HINT) && impl->hint.type)
        {
            // done
            switch (impl->hint.type)
            {
            case GB_SHAPE_TYPE_RECT:
                {
                    // make bounds
                    impl->bounds    = impl->hint.u.rect;
                    impl->flag      &= ~GB_PATH_FLAG_DIRTY_BOUNDS;

                    // trace
                    tb_trace_d("make: bounds: %{rect} from rect", &impl->bounds);
                }
                break;
            case GB_SHAPE_TYPE_CIRCLE:
                {
                    // make bounds
                    impl->bounds.x  = impl->hint.u.circle.c.x - impl->hint.u.circle.r;
                    impl->bounds.y  = impl->hint.u.circle.c.y - impl->hint.u.circle.r;
                    impl->bounds.w  = gb_lsh(impl->hint.u.circle.r, 1);
                    impl->bounds.h  = impl->bounds.w;
                    impl->flag      &= ~GB_PATH_FLAG_DIRTY_BOUNDS;

                    // trace
                    tb_trace_d("make: bounds: %{rect} from circle", &impl->bounds);
                }
                break;
            case GB_SHAPE_TYPE_ELLIPSE:
                {
                    // make bounds
                    impl->bounds.x  = impl->hint.u.ellipse.c.x - impl->hint.u.ellipse.rx;
                    impl->bounds.y  = impl->hint.u.ellipse.c.y - impl->hint.u.ellipse.ry;
                    impl->bounds.w  = gb_lsh(impl->hint.u.ellipse.rx, 1);
                    impl->bounds.h  = gb_lsh(impl->hint.u.ellipse.ry, 1);
                    impl->flag      &= ~GB_PATH_FLAG_DIRTY_BOUNDS;

                    // trace
                    tb_trace_d("make: bounds: %{rect} from ellipse", &impl->bounds);
                }
                break;
            default:
                break;
            }
        }

        // make bounds from points
        if (impl->flag & GB_PATH_FLAG_DIRTY_BOUNDS)
        {
            // the points
            gb_point_ref_t points = (gb_point_ref_t)tb_vector_data(impl->points);
            tb_assert_and_check_return_val(points, tb_null);

            // make bounds
            gb_bounds_make(&impl->bounds, points, tb_vector_size(impl->points));

            // trace
            tb_trace_d("make: bounds: %{rect} from points", &impl->bounds);

            // remove dirty
            impl->flag &= ~GB_PATH_FLAG_DIRTY_BOUNDS;
        }
    }

    // the bounds
    return &impl->bounds;
}
tb_void_t gb_path_set_convex(gb_path_ref_t path, tb_bool_t convex)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl);

    // mark convex
    if (convex) impl->flag |= GB_PATH_FLAG_CONVEX_POLYGON;
    else impl->flag &= ~GB_PATH_FLAG_CONVEX_POLYGON;
}
tb_bool_t gb_path_last(gb_path_ref_t path, gb_point_ref_t point)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return_val(impl && point, tb_false);

    // the last point
    gb_point_ref_t last = tb_null;
    if (tb_vector_size(impl->points)) last = (gb_point_ref_t)tb_vector_last(impl->points);

    // save it
    if (last) *point = *last;

    // ok?
    return last? tb_true : tb_false;
}
gb_shape_ref_t gb_path_hint(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return_val(impl, tb_null);

    // null?
    if (gb_path_null(path)) return tb_null;

    // hint dirty? remake it
    if (impl->flag & GB_PATH_FLAG_DIRTY_HINT)
    {
        // make hint
        if (!gb_path_make_hint(impl)) return tb_null;

        // remove dirty
        impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
    }

    // ok?
    return impl->hint.type != GB_SHAPE_TYPE_NONE? &impl->hint : tb_null;
}
gb_polygon_ref_t gb_path_polygon(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return_val(impl, tb_null);

    // null?
    if (gb_path_null(path)) return tb_null;

    // polygon dirty? remake it
    if (impl->flag & GB_PATH_FLAG_DIRTY_POLYGON)
    {
        // make polygon
        if (!gb_path_make_python(impl)) return tb_null; 

        // remove dirty
        impl->flag &= ~GB_PATH_FLAG_DIRTY_POLYGON;
    }

    // ok?
    return &impl->polygon;
}
tb_void_t gb_path_clos(gb_path_ref_t path)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points);

	// close it for avoiding be double closed
	if (!tb_vector_size(impl->codes) || tb_vector_last(impl->codes) != (tb_cpointer_t)GB_PATH_CODE_CLOS) 
	{
        // patch a line segment if the current point is not equal to the first point of the contour
        gb_point_t last = {0};
        if (gb_path_last(path, &last) && (last.x != impl->head.x || last.y != impl->head.y))
            gb_path_line_to(path, &impl->head);

		// append code
		tb_vector_insert_tail(impl->codes, (tb_cpointer_t)GB_PATH_CODE_CLOS);
	}

    // mark closed
    impl->flag |= GB_PATH_FLAG_CLOSED;
}
tb_void_t gb_path_move_to(gb_path_ref_t path, gb_point_ref_t point)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points && point);

	// replace the last point for avoiding one lone move-to point
	if (tb_vector_size(impl->codes) && tb_vector_last(impl->codes) == (tb_cpointer_t)GB_PATH_CODE_MOVE) 
    {
        // replace point
        tb_vector_replace_last(impl->points, point);
    }
    // move-to
    else
    {
        // append code
        tb_vector_insert_tail(impl->codes, (tb_cpointer_t)GB_PATH_CODE_MOVE);

        // append point
        tb_vector_insert_tail(impl->points, point);
    }

    // save point
    impl->head = *point;

    // clear closed
    impl->flag &= ~GB_PATH_FLAG_CLOSED;

    // mark dirty
    impl->flag |= GB_PATH_FLAG_DIRTY_ALL;
}
tb_void_t gb_path_move2_to(gb_path_ref_t path, gb_float_t x, gb_float_t y)
{
    // make point
    gb_point_t point;
    gb_point_make(&point, x, y);

    // move-to
    gb_path_move_to(path, &point);
}
tb_void_t gb_path_move2i_to(gb_path_ref_t path, tb_long_t x, tb_long_t y)
{
    // make point
    gb_point_t point;
    gb_point_imake(&point, x, y);

    // move-to
    gb_path_move_to(path, &point);
}
tb_void_t gb_path_line_to(gb_path_ref_t path, gb_point_ref_t point)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points && point);

    // closed? patch one move-to point first using the last point
    if (impl->flag & GB_PATH_FLAG_CLOSED)
    {
        // move-to the last point if exists
        gb_point_t last = {0};
        gb_path_last((gb_path_ref_t)path, &last);
        gb_path_move_to((gb_path_ref_t)path, &last);
    }

    // append code
    tb_vector_insert_tail(impl->codes, (tb_cpointer_t)GB_PATH_CODE_LINE);

    // append point
    tb_vector_insert_tail(impl->points, point);

    // mark dirty
    impl->flag |= GB_PATH_FLAG_DIRTY_ALL;
}
tb_void_t gb_path_line2_to(gb_path_ref_t path, gb_float_t x, gb_float_t y)
{
    // make point
    gb_point_t point;
    gb_point_make(&point, x, y);

    // line-to
    gb_path_line_to(path, &point);
}
tb_void_t gb_path_line2i_to(gb_path_ref_t path, tb_long_t x, tb_long_t y)
{    
    // make point
    gb_point_t point;
    gb_point_imake(&point, x, y);

    // line-to
    gb_path_line_to(path, &point);
}
tb_void_t gb_path_quad_to(gb_path_ref_t path, gb_point_ref_t ctrl, gb_point_ref_t point)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points && ctrl && point);

    // closed? patch one move-to point first using the last point
    if (impl->flag & GB_PATH_FLAG_CLOSED)
    {
        // move-to the last point if exists
        gb_point_t last = {0};
        gb_path_last((gb_path_ref_t)path, &last);
        gb_path_move_to((gb_path_ref_t)path, &last);
    }

    // append code
    tb_vector_insert_tail(impl->codes, (tb_cpointer_t)GB_PATH_CODE_QUAD);

    // append points
    tb_vector_insert_tail(impl->points, ctrl);
    tb_vector_insert_tail(impl->points, point);

    // mark dirty and curve
    impl->flag |= GB_PATH_FLAG_DIRTY_ALL | GB_PATH_FLAG_HAVE_CURVE;
}
tb_void_t gb_path_quad2_to(gb_path_ref_t path, gb_float_t cx, gb_float_t cy, gb_float_t x, gb_float_t y)
{
    // make ctrl
    gb_point_t ctrl;
    gb_point_make(&ctrl, cx, cy);

    // make point
    gb_point_t point;
    gb_point_make(&point, x, y);

    // quad-to
    gb_path_quad_to(path, &ctrl, &point);
}
tb_void_t gb_path_quad2i_to(gb_path_ref_t path, tb_long_t cx, tb_long_t cy, tb_long_t x, tb_long_t y)
{
    // make ctrl
    gb_point_t ctrl;
    gb_point_imake(&ctrl, cx, cy);

    // make point
    gb_point_t point;
    gb_point_imake(&point, x, y);

    // quad-to
    gb_path_quad_to(path, &ctrl, &point);
}
tb_void_t gb_path_cube_to(gb_path_ref_t path, gb_point_ref_t ctrl0, gb_point_ref_t ctrl1, gb_point_ref_t point)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points && ctrl0 && ctrl1 && point);

    // closed? patch one move-to point first using the last point
    if (impl->flag & GB_PATH_FLAG_CLOSED)
    {
        // move-to the last point if exists
        gb_point_t last = {0};
        gb_path_last((gb_path_ref_t)path, &last);
        gb_path_move_to((gb_path_ref_t)path, &last);
    }

    // append code
    tb_vector_insert_tail(impl->codes, (tb_cpointer_t)GB_PATH_CODE_CUBE);

    // append points
    tb_vector_insert_tail(impl->points, ctrl0);
    tb_vector_insert_tail(impl->points, ctrl1);
    tb_vector_insert_tail(impl->points, point);

    // mark dirty and curve
    impl->flag |= GB_PATH_FLAG_DIRTY_ALL | GB_PATH_FLAG_HAVE_CURVE;
}
tb_void_t gb_path_cube2_to(gb_path_ref_t path, gb_float_t cx0, gb_float_t cy0, gb_float_t cx1, gb_float_t cy1, gb_float_t x, gb_float_t y)
{
    // make ctrl0
    gb_point_t ctrl0;
    gb_point_make(&ctrl0, cx0, cy0);

    // make ctrl1
    gb_point_t ctrl1;
    gb_point_make(&ctrl1, cx1, cy1);

    // make point
    gb_point_t point;
    gb_point_make(&point, x, y);

    // cube-to
    gb_path_cube_to(path, &ctrl0, &ctrl1, &point);
}
tb_void_t gb_path_cube2i_to(gb_path_ref_t path, tb_long_t cx0, tb_long_t cy0, tb_long_t cx1, tb_long_t cy1, tb_long_t x, tb_long_t y)
{
    // make ctrl0
    gb_point_t ctrl0;
    gb_point_imake(&ctrl0, cx0, cy0);

    // make ctrl1
    gb_point_t ctrl1;
    gb_point_imake(&ctrl1, cx1, cy1);

    // make point
    gb_point_t point;
    gb_point_imake(&point, x, y);

    // cube-to
    gb_path_cube_to(path, &ctrl0, &ctrl1, &point);
}
tb_void_t gb_path_arc_to(gb_path_ref_t path, gb_arc_ref_t arc)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && arc);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_ARC;
        impl->hint.u.arc        = *arc;
        hint_maked              = tb_true;
    }

    // make quad points for arc
    gb_geometry_make_arc(arc, gb_path_make_arc_to, path);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_arc2_to(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t rx, gb_float_t ry, gb_float_t ab, gb_float_t an)
{
    // make arc
    gb_arc_t arc;
    gb_arc_make(&arc, x0, y0, rx, ry, ab, an);

    // arc-to
    gb_path_arc_to(path, &arc);
}
tb_void_t gb_path_arc2i_to(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_size_t rx, tb_size_t ry, tb_long_t ab, tb_long_t an)
{
    // make arc
    gb_arc_t arc;
    gb_arc_imake(&arc, x0, y0, rx, ry, ab, an);

    // arc-to
    gb_path_arc_to(path, &arc);
}
tb_void_t gb_path_add_path(gb_path_ref_t path, gb_path_ref_t added)
{
    // null? copy it
    if (gb_path_null(path)) gb_path_copy(path, added);
    // add it
    else
    {
        // done
        tb_for_all_if (gb_path_item_ref_t, item, added, item)
        {
            switch (item->code)
            {
            case GB_PATH_CODE_MOVE:
                gb_path_move_to(path, &item->point);
                break;
            case GB_PATH_CODE_LINE:
                gb_path_line_to(path, &item->point);
                break;
            case GB_PATH_CODE_QUAD:
                gb_path_quad_to(path, &item->ctrls[0], &item->point);
                break;
            case GB_PATH_CODE_CUBE:
                gb_path_cube_to(path, &item->ctrls[0], &item->ctrls[1], &item->point);
                break;
            case GB_PATH_CODE_CLOS:
                gb_path_clos(path);
                break;
            default:
                // trace
                tb_trace_e("invalid code: %lu", item->code);
                break;
            }
        }
    }
}
tb_void_t gb_path_add_line(gb_path_ref_t path, gb_line_ref_t line)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && line);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_LINE;
        impl->hint.u.line       = *line;
        impl->flag              &= ~GB_PATH_FLAG_DIRTY_HINT;
        hint_maked              = tb_true;
    }

    // add line
    gb_path_move_to(path, &line->p0);
    gb_path_line_to(path, &line->p1);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_line2(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t x1, gb_float_t y1)
{
    // make line
    gb_line_t line;
    gb_line_make(&line, x0, y0, x1, y1);

    // add line
    gb_path_add_line(path, &line);
}
tb_void_t gb_path_add_line2i(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_long_t x1, tb_long_t y1)
{
    // make line
    gb_line_t line;
    gb_line_imake(&line, x0, y0, x1, y1);

    // add line
    gb_path_add_line(path, &line);
}
tb_void_t gb_path_add_arc(gb_path_ref_t path, gb_arc_ref_t arc)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && impl->codes && impl->points && arc);

    // ellipse? add it
    if (arc->an >= GB_DEGREE_360 || arc->an <= -GB_DEGREE_360)
    {
        // make ellipse
        gb_ellipse_t ellipse;
        gb_ellipse_make(&ellipse, arc->c.x, arc->c.y, arc->rx, arc->ry);

        // add ellipse
        gb_path_add_ellipse(path, &ellipse, gb_bz(arc->an)? GB_ROTATE_DIRECTION_CW : GB_ROTATE_DIRECTION_CCW);
        return ;
    }

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_ARC;
        impl->hint.u.arc        = *arc;
        hint_maked              = tb_true;
    }

    // make quad points for arc
    gb_geometry_make_arc(arc, gb_path_make_add_arc, path);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_arc2(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t rx, gb_float_t ry, gb_float_t ab, gb_float_t an)
{
    // make arc
    gb_arc_t arc;
    gb_arc_make(&arc, x0, y0, rx, ry, ab, an);

    // add arc
    gb_path_add_arc(path, &arc);
}
tb_void_t gb_path_add_arc2i(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_size_t rx, tb_size_t ry, tb_long_t ab, tb_long_t an)
{
    // make arc
    gb_arc_t arc;
    gb_arc_imake(&arc, x0, y0, rx, ry, ab, an);

    // add arc
    gb_path_add_arc(path, &arc);
}
tb_void_t gb_path_add_triangle(gb_path_ref_t path, gb_triangle_ref_t triangle)
{
	// check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
	tb_assert_and_check_return(impl && triangle);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_TRIANGLE;
        impl->hint.u.triangle   = *triangle;
        hint_maked              = tb_true;
    }

    // add triangle
    gb_path_move_to(path, &triangle->p0);
    gb_path_line_to(path, &triangle->p1);
    gb_path_line_to(path, &triangle->p2);
    gb_path_clos(path);
    
    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_triangle2(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t x1, gb_float_t y1, gb_float_t x2, gb_float_t y2)
{
    // make triangle
    gb_triangle_t triangle;
    gb_triangle_make(&triangle, x0, y0, x1, y1, x2, y2);

    // add triangle
    gb_path_add_triangle(path, &triangle);
}
tb_void_t gb_path_add_triangle2i(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_long_t x1, tb_long_t y1, tb_long_t x2, tb_long_t y2)
{
    // make triangle
    gb_triangle_t triangle;
    gb_triangle_imake(&triangle, x0, y0, x1, y1, x2, y2);

    // add triangle
    gb_path_add_triangle(path, &triangle);
}
tb_void_t gb_path_add_rect(gb_path_ref_t path, gb_rect_ref_t rect, tb_size_t direction)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && rect);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_RECT;
        impl->hint.u.rect       = *rect;
        hint_maked              = tb_true;
    }

    // add rect
    gb_path_move2_to(path, rect->x, rect->y);
    if (direction == GB_ROTATE_DIRECTION_CW)
    {
        gb_path_line2_to(path, rect->x + rect->w, rect->y);
        gb_path_line2_to(path, rect->x + rect->w, rect->y + rect->h);
        gb_path_line2_to(path, rect->x, rect->y + rect->h);
    }
    else
    {
        gb_path_line2_to(path, rect->x, rect->y + rect->h);
        gb_path_line2_to(path, rect->x + rect->w, rect->y + rect->h);
        gb_path_line2_to(path, rect->x + rect->w, rect->y);
    }
    gb_path_clos(path);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_rect2(gb_path_ref_t path, gb_float_t x, gb_float_t y, gb_float_t w, gb_float_t h, tb_size_t direction)
{
    // make rect
    gb_rect_t rect;
    gb_rect_make(&rect, x, y, w, h);

    // add rect
    gb_path_add_rect(path, &rect, direction);
}
tb_void_t gb_path_add_rect2i(gb_path_ref_t path, tb_long_t x, tb_long_t y, tb_size_t w, tb_size_t h, tb_size_t direction)
{
    // make rect
    gb_rect_t rect;
    gb_rect_imake(&rect, x, y, w, h);

    // add rect
    gb_path_add_rect(path, &rect, direction);
}
tb_void_t gb_path_add_round_rect(gb_path_ref_t path, gb_round_rect_ref_t rect, tb_size_t direction)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && rect);

    // is rect? 
    if (gb_round_rect_is_rect(rect))
    {
        // add rect
        gb_path_add_rect(path, &rect->bounds, direction);
        return ;
    }
    // is ellipse?
    else if (gb_round_rect_is_ellipse(rect))
    {
        // make ellipse
        gb_ellipse_t ellipse;
        gb_ellipse_make_from_rect(&ellipse, &rect->bounds);

        // add ellipse
        gb_path_add_ellipse(path, &ellipse, direction);
        return ;
    }

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_ROUND_RECT;
        impl->hint.u.round_rect = *rect;
        hint_maked              = tb_true;
    }

    // the bounds
    gb_float_t x = rect->bounds.x;
    gb_float_t y = rect->bounds.y;
    gb_float_t w = rect->bounds.w;
    gb_float_t h = rect->bounds.h;

    // the radius
    gb_float_t rx1 = rect->radius[GB_RECT_CORNER_LT].x;
    gb_float_t ry1 = rect->radius[GB_RECT_CORNER_LT].y;
    gb_float_t rx2 = rect->radius[GB_RECT_CORNER_RT].x;
    gb_float_t ry2 = rect->radius[GB_RECT_CORNER_RT].y;
    gb_float_t rx3 = rect->radius[GB_RECT_CORNER_RB].x;
    gb_float_t ry3 = rect->radius[GB_RECT_CORNER_RB].y;
    gb_float_t rx4 = rect->radius[GB_RECT_CORNER_LB].x;
    gb_float_t ry4 = rect->radius[GB_RECT_CORNER_LB].y;

    // add the round rect
    gb_path_move2_to(path, x, y + ry1);
    if (direction == GB_ROTATE_DIRECTION_CW)
    {
        gb_path_arc2_to(path,   x + rx1,        y + ry1,        rx1, ry1, -GB_DEGREE_180,   GB_DEGREE_90    );
        gb_path_line2_to(path,  x + w - rx2,    y                                                           );
        gb_path_arc2_to(path,   x + w - rx2,    y + ry2,        rx2, ry2, -GB_DEGREE_90,    GB_DEGREE_90    );
        gb_path_line2_to(path,  x + w,          y + h - ry3                                                 );
        gb_path_arc2_to(path,   x + w - rx2,    y + h - ry3,    rx3, ry3, 0,                GB_DEGREE_90    );
        gb_path_line2_to(path,  x + rx4,        y + h                                                       );
        gb_path_arc2_to(path,   x + rx4,        y + h - ry4,    rx4, ry4, GB_DEGREE_90,     GB_DEGREE_90    );
        gb_path_line2_to(path,  x,              y + ry1                                                     );
    }
    else
    {
        gb_path_line2_to(path,  x,              y + h - ry4                                                 );
        gb_path_arc2_to(path,   x + rx4,        y + h - ry4,    rx4, ry4, GB_DEGREE_180,    -GB_DEGREE_90   );
        gb_path_line2_to(path,  x + w - rx3,    y + h                                                       );
        gb_path_arc2_to(path,   x + w - rx2,    y + h - ry3,    rx3, ry3, GB_DEGREE_90,     -GB_DEGREE_90   );
        gb_path_line2_to(path,  x + w,          y + ry3                                                     );
        gb_path_arc2_to(path,   x + w - rx2,    y + ry2,        rx2, ry2, 0,                -GB_DEGREE_90   );
        gb_path_line2_to(path,  x + rx1,        y                                                           );
        gb_path_arc2_to(path,   x + rx1,        y + ry1,        rx1, ry1, -GB_DEGREE_90,    -GB_DEGREE_90   );
    }
    gb_path_clos(path);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_round_rect2(gb_path_ref_t path, gb_rect_ref_t bounds, gb_float_t rx, gb_float_t ry, tb_size_t direction)
{
    // make rect
    gb_round_rect_t rect;
    gb_round_rect_make_same(&rect, bounds, rx, ry);

    // draw rect
    gb_path_add_round_rect(path, &rect, direction);
}
tb_void_t gb_path_add_round_rect2i(gb_path_ref_t path, gb_rect_ref_t bounds, tb_size_t rx, tb_size_t ry, tb_size_t direction)
{
    // make rect
    gb_round_rect_t rect;
    gb_round_rect_imake_same(&rect, bounds, rx, ry);

    // draw rect
    gb_path_add_round_rect(path, &rect, direction);
}
tb_void_t gb_path_add_circle(gb_path_ref_t path, gb_circle_ref_t circle, tb_size_t direction)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && circle);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_CIRCLE;
        impl->hint.u.circle    = *circle;
        hint_maked              = tb_true;

        // @note remove dirty first before adding ellipse
        impl->flag              &= ~GB_PATH_FLAG_DIRTY_HINT;
    }

    // make ellipse
    gb_ellipse_t ellipse;
    gb_ellipse_make(&ellipse, circle->c.x, circle->c.y, circle->r, circle->r);

    // add ellipse
    gb_path_add_ellipse(path, &ellipse, direction);
    
    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_circle2(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t r, tb_size_t direction)
{
    // make circle
    gb_circle_t circle;
    gb_circle_make(&circle, x0, y0, r);

    // add circle
    gb_path_add_circle(path, &circle, direction);
}
tb_void_t gb_path_add_circle2i(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_size_t r, tb_size_t direction)
{
    // make circle
    gb_circle_t circle;
    gb_circle_imake(&circle, x0, y0, r);

    // add circle
    gb_path_add_circle(path, &circle, direction);
}
tb_void_t gb_path_add_ellipse(gb_path_ref_t path, gb_ellipse_ref_t ellipse, tb_size_t direction)
{
    // check
    gb_path_impl_t* impl = (gb_path_impl_t*)path;
    tb_assert_and_check_return(impl && ellipse);

    // null and dirty? make hint
    tb_bool_t hint_maked = tb_false;
    if (gb_path_null(path) && (impl->flag & GB_PATH_FLAG_DIRTY_HINT))
    {
        impl->hint.type         = GB_SHAPE_TYPE_ELLIPSE;
        impl->hint.u.ellipse    = *ellipse;
        hint_maked              = tb_true;
    }

    // init center and radius
    gb_float_t rx = ellipse->rx;
    gb_float_t ry = ellipse->ry;
    gb_float_t x0 = ellipse->c.x;
    gb_float_t y0 = ellipse->c.y;

    // init factor
    gb_float_t sx = gb_mul(rx, GB_TAN_PIOVER8); //< tan(pi/8)
    gb_float_t sy = gb_mul(ry, GB_TAN_PIOVER8);
    gb_float_t mx = gb_mul(rx, GB_SQRT2_OVER2); //< sqrt(2)/2
    gb_float_t my = gb_mul(ry, GB_SQRT2_OVER2);

    // init bounds
    gb_float_t x1 = x0 - rx; 
    gb_float_t y1 = y0 - ry;
    gb_float_t x2 = x0 + rx;
    gb_float_t y2 = y0 + ry;

    /* make ellipse path
     * 
     * <pre>
     * circle: 
     * - quad_to(c, p)
     * - p(x0 + r * sqrt(2) / 2, y0 - r * sqrt(2)/2)
     * - c(x2, y0 - r * tan(pi/8))
     *
     * (x1, y1)
     *  ---------------------------
     * |             |           / |
     * |             |      p  /   |
     * |             |      *      * c 
     * |             |   /         | 
     * |             |/ 45         |
     * |- - - - - - - - - - - - - -| (move-to)
     * |          (x0, y0)         |
     * |             |             |
     * |             |             |
     * |             |             |
     * |             |             |
     *  --------------------------- 
     *                      (x2, y2)
     *
     *
     * ellipse: 
     * - quad_to(c, p)
     * - p(x0 + rx * sqrt(2) / 2, y0 - ry * sqrt(2)/2)
     * - c(x2, y0 - ry * tan(pi/8))
     *
     * (x1, y1)
     *  ------------------------------------------
     * |                     |           /        |
     * |                     |      p  /          |
     * |                     |      *             * c 
     * |                     |   /                | 
     * |                     |/ 45                |
     * |- - - - - - - - - - - - - - - - - - - - - | (move-to)
     * |                  (x0, y0)                |
     * |                     |                    |
     * |                     |                    |
     * |                     |                    |
     * |                     |                    |
     *  ------------------------------------------ 
     *                                     (x2, y2)
     *
     *
     * </pre>
     */
    gb_path_move2_to(path, x2, y0);
    if (direction == GB_ROTATE_DIRECTION_CW)
    {
        gb_path_quad2_to(path, x2,          y0 + sy,    x0 + mx,    y0 + my );
        gb_path_quad2_to(path, x0 + sx,     y2,         x0,         y2      );
        gb_path_quad2_to(path, x0 - sx,     y2,         x0 - mx,    y0 + my );
        gb_path_quad2_to(path, x1,          y0 + sy,    x1,         y0      );
        gb_path_quad2_to(path, x1,          y0 - sy,    x0 - mx,    y0 - my );
        gb_path_quad2_to(path, x0 - sx,     y1,         x0,         y1      );
        gb_path_quad2_to(path, x0 + sx,     y1,         x0 + mx,    y0 - my );
        gb_path_quad2_to(path, x2,          y0 - sy,    x2,         y0      );
    }
    else
    {
        gb_path_quad2_to(path, x2,          y0 - sy,    x0 + mx,    y0 - my );
        gb_path_quad2_to(path, x0 + sx,     y1,         x0,         y1      );
        gb_path_quad2_to(path, x0 - sx,     y1,         x0 - mx,    y0 - my );
        gb_path_quad2_to(path, x1,          y0 - sy,    x1,         y0      );
        gb_path_quad2_to(path, x1,          y0 + sy,    x0 - mx,    y0 + my );
        gb_path_quad2_to(path, x0 - sx,     y2,         x0,         y2      );
        gb_path_quad2_to(path, x0 + sx,     y2,         x0 + mx,    y0 + my );
        gb_path_quad2_to(path, x2,          y0 + sy,    x2,         y0      );
    }
    gb_path_clos(path);

    // hint have been maked? remove dirty
    if (hint_maked) impl->flag &= ~GB_PATH_FLAG_DIRTY_HINT;
}
tb_void_t gb_path_add_ellipse2(gb_path_ref_t path, gb_float_t x0, gb_float_t y0, gb_float_t rx, gb_float_t ry, tb_size_t direction)
{
    // make ellipse
    gb_ellipse_t ellipse;
    gb_ellipse_make(&ellipse, x0, y0, rx, ry);

    // add ellipse
    gb_path_add_ellipse(path, &ellipse, direction);
}
tb_void_t gb_path_add_ellipse2i(gb_path_ref_t path, tb_long_t x0, tb_long_t y0, tb_size_t rx, tb_size_t ry, tb_size_t direction)
{
    // make ellipse
    gb_ellipse_t ellipse;
    gb_ellipse_imake(&ellipse, x0, y0, rx, ry);

    // add ellipse
    gb_path_add_ellipse(path, &ellipse, direction);
}
#ifdef __gb_debug__
tb_void_t gb_path_dump(gb_path_ref_t path)
{
    // check
    tb_assert_and_check_return(path);

    // trace
    tb_trace_i("");

    // trace last point
    gb_point_t last = {0};
    if (gb_path_last(path, &last)) tb_trace_i("last: %{point}", &last);

    // trace bounds
    tb_trace_i("bounds: %{rect}", gb_path_bounds(path));

    // trace points
    tb_for_all_if (gb_path_item_ref_t, item, path, item)
    {
        switch (item->code)
        {
        case GB_PATH_CODE_MOVE:
            tb_trace_i("move_to: %{point}", &item->point);
            break;
        case GB_PATH_CODE_LINE:
            tb_trace_i("line_to: %{point}", &item->point);
            break;
        case GB_PATH_CODE_QUAD:
            tb_trace_i("quad_to: %{point}, %{point}", &item->ctrls[0], &item->point);
            break;
        case GB_PATH_CODE_CUBE:
            tb_trace_i("cube_to: %{point}, %{point}, %{point}", &item->ctrls[0], &item->ctrls[1], &item->point);
            break;
        case GB_PATH_CODE_CLOS:
            tb_trace_i("closed");
            break;
        default:
            break;
        }
    }
}
#endif

