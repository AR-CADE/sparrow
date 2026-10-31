#ifndef SPARROW_SESSION_MANIFEST_HPP
#define SPARROW_SESSION_MANIFEST_HPP

class SparrowView;

void sparrow_session_manifest_init();
void sparrow_session_manifest_flush();
bool sparrow_session_manifest_apply_for_view(SparrowView *view);

#endif // SPARROW_SESSION_MANIFEST_HPP
