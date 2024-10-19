extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/avstring.h>
}

#include "error.h"

#define WHITESPACES " \n\t\r"

// print an error message if some options were not found
static void log_unknown_opt(const AVFilterGraphSegment *seg)
{
    for (size_t i = 0; i < seg->nb_chains; i++) {
        const AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            const AVFilterParams *p = ch->filters[j];
            const AVDictionaryEntry *e;

            if (!p->filter)
                continue;

            e = av_dict_iterate(p->opts, NULL);

            if (e) {
                av_log(p->filter, AV_LOG_ERROR,
                       "Could not set non-existent option '%s' to value '%s'\n",
                       e->key, e->value);
                return;
            }
        }
    }

}

/**
 * Parse the name of a link, which has the format "[linkname]".
 *
 * @return a pointer (that need to be freed after use) to the name
 * between parenthesis
 */
static char *parse_link_name(const char **buf, void *log_ctx)
{
    const char *start = *buf;
    char *name;
    (*buf)++;

    name = av_get_token(buf, "]");
    if (!name)
        return NULL;

    if (!name[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if (**buf != ']') {
        av_log(log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(&name);
        return NULL;
    }
    (*buf)++;

    return name;
}

static void pad_params_free(AVFilterPadParams **pfpp)
{
    AVFilterPadParams *fpp = *pfpp;

    if (!fpp)
        return;

    av_freep(&fpp->label);

    av_freep(pfpp);
}

static int linklabels_parse(void *logctx, const char **linklabels,
                            AVFilterPadParams ***res, unsigned *nb_res)
{
    AVFilterPadParams **pp = NULL;
    int nb = 0;
    int ret;

    while (**linklabels == '[') {
        char *label;
        AVFilterPadParams *par;

        label = parse_link_name(linklabels, logctx);
        if (!label) {
            ret = AVERROR(EINVAL);
            goto fail;
        }

        par = (AVFilterPadParams *)av_mallocz(sizeof(*par));
        if (!par) {
            av_freep(&label);
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        par->label = label;

        ret = av_dynarray_add_nofree(&pp, &nb, par);
        if (ret < 0) {
            pad_params_free(&par);
            goto fail;
        }

        *linklabels += strspn(*linklabels, WHITESPACES);
    }

    *res    = pp;
    *nb_res = nb;

    return 0;
fail:
    for (int i = 0; i < nb; i++)
        pad_params_free(&pp[i]);
    av_freep(&pp);
    return ret;
}

static AVFilterInOut *extract_inout(const char *label, AVFilterInOut **links)
{
    AVFilterInOut *ret;

    while (*links && (!(*links)->name || strcmp((*links)->name, label)))
        links = &((*links)->next);

    ret = *links;

    if (ret) {
        *links = ret->next;
        ret->next = NULL;
    }

    return ret;
}

static void append_inout(AVFilterInOut **inouts, AVFilterInOut **element)
{
    while (*inouts && (*inouts)->next)
        inouts = &((*inouts)->next);

    if (!*inouts)
        *inouts = *element;
    else
        (*inouts)->next = *element;
    *element = NULL;
}

int avfilter_graph_parse_ptr2(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut **open_inputs_ptr, AVFilterInOut **open_outputs_ptr,
                         AVBufferRef *hw_device)
{
    AVFilterInOut *user_inputs  = open_inputs_ptr  ? *open_inputs_ptr  : NULL;
    AVFilterInOut *user_outputs = open_outputs_ptr ? *open_outputs_ptr : NULL;

    AVFilterInOut *inputs = NULL, *outputs = NULL;
    AVFilterGraphSegment *seg = NULL;
    AVFilterChain         *ch;
    AVFilterParams         *p;
    int ret;

    ret = avfilter_graph_segment_parse(graph, filters, 0, &seg);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_segment_create_filters(seg, 0);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_segment_apply_opts(seg, 0);
    if (ret < 0) {
        if (ret == AVERROR_OPTION_NOT_FOUND)
            log_unknown_opt(seg);
        goto end;
    }

    if (hw_device) {
        for (unsigned int i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *f = graph->filters[i];

            if (!(f->filter->flags & AVFILTER_FLAG_HWDEVICE))
                continue;
            if (f->hw_device_ctx)
            {
                av_log(NULL, AV_LOG_ERROR, "Hardware device context ref already set on %s\n", f->name);
            }
            f->hw_device_ctx = av_buffer_ref(hw_device);
            if (!f->hw_device_ctx) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
        }
    }

    ret = avfilter_graph_segment_init(seg, 0);
    if (ret < 0)
        goto end;

    /* First input pad, assume it is "[in]" if not specified */
    p = seg->chains[0]->filters[0];
    if (p->filter->nb_inputs == 1 && !p->inputs) {
        const char *tmp = "[in]";

        ret = linklabels_parse(graph, &tmp, &p->inputs, &p->nb_inputs);
        if (ret < 0)
            goto end;
    }

    /* Last output pad, assume it is "[out]" if not specified */
    ch = seg->chains[seg->nb_chains - 1];
    p = ch->filters[ch->nb_filters - 1];
    if (p->filter->nb_outputs == 1 && !p->outputs) {
        const char *tmp = "[out]";

        ret = linklabels_parse(graph, &tmp, &p->outputs, &p->nb_outputs);
        if (ret < 0)
            goto end;
    }

    ret = avfilter_graph_segment_apply(seg, 0, &inputs, &outputs);
    avfilter_graph_segment_free(&seg);
    if (ret < 0)
        goto end;

    // process user-supplied inputs/outputs
    while (inputs) {
        AVFilterInOut *cur, *match = NULL;

        cur       = inputs;
        inputs    = cur->next;
        cur->next = NULL;

        if (cur->name)
            match = extract_inout(cur->name, &user_outputs);

        if (match) {
            ret = avfilter_link(match->filter_ctx, match->pad_idx,
                                cur->filter_ctx, cur->pad_idx);
            avfilter_inout_free(&match);
            avfilter_inout_free(&cur);
            if (ret < 0)
                goto end;
        } else
            append_inout(&user_inputs, &cur);
    }
    while (outputs) {
        AVFilterInOut *cur, *match = NULL;

        cur       = outputs;
        outputs   = cur->next;
        cur->next = NULL;

        if (cur->name)
            match = extract_inout(cur->name, &user_inputs);

        if (match) {
            ret = avfilter_link(cur->filter_ctx, cur->pad_idx,
                                match->filter_ctx, match->pad_idx);
            avfilter_inout_free(&match);
            avfilter_inout_free(&cur);
            if (ret < 0)
                goto end;
        } else
            append_inout(&user_outputs, &cur);
    }

end:
    avfilter_graph_segment_free(&seg);

    if (ret < 0) {
        av_log(graph, AV_LOG_ERROR, "Error processing filtergraph: %s\n",
               AVErrorString(ret).c_str());

        while (graph->nb_filters)
            avfilter_free(graph->filters[0]);
        av_freep(&graph->filters);
    }

    /* clear open_in/outputs only if not passed as parameters */
    if (open_inputs_ptr) *open_inputs_ptr = user_inputs;
    else avfilter_inout_free(&user_inputs);
    if (open_outputs_ptr) *open_outputs_ptr = user_outputs;
    else avfilter_inout_free(&user_outputs);

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
