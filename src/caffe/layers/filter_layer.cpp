#include <vector>

#include "caffe/layers/filter_layer.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template<typename Dtype, typename MItype, typename MOtype>
void FilterLayer<Dtype, MItype, MOtype>::LayerSetUp(const vector<Blob<MItype>*>& bottom,
      const vector<Blob<MOtype>*>& top) {
  CHECK_EQ(top.size(), bottom.size() - 1);
  first_reshape_ = true;
}

template<typename Dtype, typename MItype, typename MOtype>
void FilterLayer<Dtype, MItype, MOtype>::Reshape(const vector<Blob<MItype>*>& bottom,
      const vector<Blob<MOtype>*>& top) {
  // bottom[0...k-1] are the blobs to filter
  // bottom[last] is the "selector_blob"
  int_tp selector_index = bottom.size() - 1;
  for (int_tp i = 1; i < bottom[selector_index]->num_axes(); ++i) {
    CHECK_EQ(bottom[selector_index]->shape(i), 1)
        << "Selector blob dimensions must be singletons (1), except the first";
  }
  for (int_tp i = 0; i < bottom.size() - 1; ++i) {
    CHECK_EQ(bottom[selector_index]->shape(0), bottom[i]->shape(0)) <<
        "Each bottom should have the same 0th dimension as the selector blob";
  }

  const Dtype* bottom_data_selector = bottom[selector_index]->cpu_data();
  indices_to_forward_.clear();

  // look for non-zero elements in bottom[0]. Items of each bottom that
  // have the same index as the items in bottom[0] with value == non-zero
  // will be forwarded
  for (int_tp item_id = 0; item_id < bottom[selector_index]->shape(0);
      ++item_id) {
    // we don't need an offset because item size == 1
    const Dtype* tmp_data_selector = bottom_data_selector + item_id;
    if (*tmp_data_selector) {
      indices_to_forward_.push_back(item_id);
    }
  }
  // only filtered items will be forwarded
  int_tp new_tops_num = indices_to_forward_.size();
  // init
  if (first_reshape_) {
    new_tops_num = bottom[0]->shape(0);
    first_reshape_ = false;
  }
  for (int_tp t = 0; t < top.size(); ++t) {
    int_tp num_axes = bottom[t]->num_axes();
    vector<int_tp> shape_top(num_axes);
    shape_top[0] = new_tops_num;
    for (int_tp ts = 1; ts < num_axes; ++ts)
      shape_top[ts] = bottom[t]->shape(ts);
    top[t]->Reshape(shape_top);
  }
}

template<typename Dtype, typename MItype, typename MOtype>
void FilterLayer<Dtype, MItype, MOtype>::Forward_cpu(const vector<Blob<MItype>*>& bottom,
      const vector<Blob<MOtype>*>& top) {
  int_tp new_tops_num = indices_to_forward_.size();
  // forward all filtered items for all bottoms but the Selector (bottom[last])
  for (int_tp t = 0; t < top.size(); ++t) {
    const Dtype* bottom_data = bottom[t]->cpu_data();
    Dtype* top_data = top[t]->mutable_cpu_data();
    int_tp dim = bottom[t]->count() / bottom[t]->shape(0);
    for (int_tp n = 0; n < new_tops_num; ++n) {
      int_tp data_offset_top = n * dim;
      int_tp data_offset_bottom = indices_to_forward_[n] * bottom[t]->count(1);
      caffe_copy(dim, bottom_data + data_offset_bottom,
          top_data + data_offset_top);
    }
  }
}

template<typename Dtype, typename MItype, typename MOtype>
void FilterLayer<Dtype, MItype, MOtype>::Backward_cpu(const vector<Blob<MOtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<MItype>*>& bottom) {
  if (propagate_down[bottom.size() - 1]) {
    LOG(FATAL) << this->type()
               << "Layer cannot backpropagate to filter index inputs";
  }
  for (int_tp i = 0; i < top.size(); i++) {
    // bottom[last] is the selector and never needs backpropagation
    // so we can iterate over top vector because top.size() == bottom.size() -1
    if (propagate_down[i]) {
      const int_tp dim = top[i]->count() / top[i]->shape(0);
      int_tp next_to_backward_offset = 0;
      int_tp batch_offset = 0;
      int_tp data_offset_bottom = 0;
      int_tp data_offset_top = 0;
      for (int_tp n = 0; n < bottom[i]->shape(0); n++) {
        data_offset_bottom = n * dim;
        if (next_to_backward_offset >= indices_to_forward_.size()) {
          // we already visited all items that were been forwarded, so
          // just set to zero remaining ones
          caffe_set(dim, Dtype(0),
              bottom[i]->mutable_cpu_diff() + data_offset_bottom);
        } else {
          batch_offset = indices_to_forward_[next_to_backward_offset];
          if (n != batch_offset) {  // this data was not been forwarded
            caffe_set(dim, Dtype(0),
                bottom[i]->mutable_cpu_diff() + data_offset_bottom);
          } else {  // this data was been forwarded
            data_offset_top = next_to_backward_offset * dim;
            next_to_backward_offset++;  // point to next forwarded item index
            caffe_copy(dim, top[i]->mutable_cpu_diff() + data_offset_top,
                bottom[i]->mutable_cpu_diff() + data_offset_bottom);
          }
        }
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(FilterLayer);
#endif

INSTANTIATE_CLASS_3T_GUARDED(FilterLayer, (half_fp), (half_fp), (half_fp));
INSTANTIATE_CLASS_3T_GUARDED(FilterLayer, (float), (float), (float));
INSTANTIATE_CLASS_3T_GUARDED(FilterLayer, (double), (double), (double));

REGISTER_LAYER_CLASS(Filter);
REGISTER_LAYER_CLASS_INST(Filter, (half_fp), (half_fp), (half_fp));
REGISTER_LAYER_CLASS_INST(Filter, (float), (float), (float));
REGISTER_LAYER_CLASS_INST(Filter, (double), (double), (double));

}  // namespace caffe
